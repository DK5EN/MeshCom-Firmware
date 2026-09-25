#!/usr/bin/env python3
"""Extract the fixture for test/test_nbr_replay from raw firmware log(s).

MeshCom 5 topology, wave 1 (docs/meshcom5-campaign.md), brief W1a. The host
replay test (test/test_nbr_replay/test_nbr_replay.cpp) feeds a real field
capture through the CURRENT dense neighbour matrix (src/nbr_matrix.{h,cpp})
and checks that it reproduces the capture's [NBR] lines. It only needs two
kinds of raw-log lines:

  - "[LOG] ..." frame lines (printBuffer_aprs() in src/loop_functions.cpp):
    carry the decoded path, type, RSSI/SNR and the on-device millis() (the
    trailing "t=<ms>") that the replay derives now_min from.
  - "[NBR]|..." lines (nbrLog() callback, src/nbr_matrix.cpp): the actual
    matrix instrumentation the replay must reproduce.

Everything else (KEEP/DATA/GW/MC-DBG/BEAT/MH-LoRa echoes/RX-LoRa2/raw radio
prints/...) is replay-irrelevant noise and is dropped. This keeps the fixture
small (see docs/nbr-logformat.md for the [NBR] line contract) and avoids
shipping multi-MB raw captures into the repo.

Usage (single file, from the start):
    python3 tools/nbr_replay_extract.py \\
        --input ~/Downloads/dk5en-98-nbr/2026-09-24.log \\
        --output test/test_nbr_replay/fixtures/dk5en98-20260924.txt

Usage (cold boot: start mid-file at the device's last millis() reset, then
concatenate the following day so the replay's nbrInit() lines up with the
real one -- see ASSUMPTION 2 in test/test_nbr_replay/test_nbr_replay.cpp's
header comment for why a mid-session fixture cannot reproduce EVICT/CANCEL/
REFUSE byte-identically):
    python3 tools/nbr_replay_extract.py \\
        --input ~/Downloads/dk5en-98-nbr/2026-09-23.log --from-last-reset \\
        --input ~/Downloads/dk5en-98-nbr/2026-09-24.log \\
        --output test/test_nbr_replay/fixtures/dk5en98-20260923-boot.txt

--input may be repeated (append, in the order given) to concatenate several
days in chronological order; only the FIRST --input honours --start-line /
--from-last-reset -- every subsequent file is read from its own line 1. With
a single --input and neither flag, this is the original single-file, from-
line-1 behaviour.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# A "[LOG]" frame line as printBuffer_aprs() formats it (src/loop_functions.cpp):
#   "%s %s %03i %c x%08X H%02X S%i T%i M%02X %s>%s%c%s HW:%02i MOD:%01X/%01i
#    FCS:%04X FW:%02i:%c LH:%02X%s"
# with tail = setlogFormatRxTail() -> " RSSI:%d SNR:%d DUP:%c OWN:%c t=%lu"
# (src/setlog_lines.cpp). The path/payload in the middle is free text and is
# deliberately NOT matched here -- only the fixed head and the fixed tail,
# so a payload that happens to contain e.g. "H01" or "RSSI:" does not defeat
# the match.
LOG_LINE_RE = re.compile(
    r"\[LOG\] \d+ [!@:] x[0-9A-Fa-f]{8} H[0-9A-Fa-f]+ S\d+ T\d+ M\d+ .*"
    r"RSSI:-?\d+ SNR:-?\d+ DUP:[a-z] OWN:[a-z-] t=\d+\s*$"
)

# Any nbrLog() line (docs/nbr-logformat.md): "[NBR]|<TYPE>|...".
NBR_LINE_RE = re.compile(r"\[NBR\]\|")

# The device's own millis() clock, wherever a raw-log line ends in it: every
# "[LOG] ..." variant (printBuffer_aprs()'s RX tail, but also the shorter TX/
# GWU/RLY prints) ends its own frame-log line in exactly " t=<ms>" with
# nothing after it. The negative lookbehind is required: "wait=4612" and
# "slot=19" both contain the literal substring "t=<digits>" too, just not at
# a word boundary -- without it this mistakes a queue-wait or ring-slot value
# for a millis() reading and finds hundreds of fake "resets" a minute apart
# (verified against 2026-09-23.log while building this: dropping either the
# lookbehind or the end-of-line anchor drags in "[MC-STAT] t=300s ..." and
# "RLY ... slot=19" respectively).
RESET_PROBE_RE = re.compile(r"\[LOG\].*(?<![A-Za-z0-9_])t=(\d+)\s*$")


def should_keep(line: str) -> bool:
    return bool(NBR_LINE_RE.search(line)) or bool(LOG_LINE_RE.search(line))


def find_last_reset_line(input_path: Path) -> int | None:
    """1-indexed line number of the LAST millis() reset in input_path, or
    None if the file has none (a clean single-boot capture).

    A "reset" is any "[LOG]"-line millis() reading smaller than the previous
    one seen in the file -- the device rebooted between those two lines. The
    returned line is the FIRST line of the new (post-reboot) session, i.e.
    the line extract() should start reading from.
    """
    prev_t: int | None = None
    last_reset_line: int | None = None
    with input_path.open("r", encoding="utf-8", errors="replace") as f:
        for lineno, line in enumerate(f, start=1):
            m = RESET_PROBE_RE.search(line)
            if not m:
                continue
            t = int(m.group(1))
            if prev_t is not None and t < prev_t:
                last_reset_line = lineno
            prev_t = t
    return last_reset_line


def extract(input_path: Path, output_path: Path, start_line: int = 1, append: bool = False) -> tuple[int, int, int]:
    """Copy the relevant lines from input_path, starting at start_line
    (1-indexed, inclusive), to output_path (overwritten unless append=True).

    Returns (lines_read_from_start_line, lines_kept, bytes_now_in_output).
    """
    lines_read = 0
    lines_kept = 0

    output_path.parent.mkdir(parents=True, exist_ok=True)

    mode = "a" if append else "w"
    with input_path.open("r", encoding="utf-8", errors="replace") as src, \
         output_path.open(mode, encoding="utf-8", newline="\n") as dst:
        for lineno, line in enumerate(src, start=1):
            if lineno < start_line:
                continue
            lines_read += 1
            if should_keep(line):
                dst.write(line.rstrip("\r\n") + "\n")
                lines_kept += 1

    bytes_written = output_path.stat().st_size
    return lines_read, lines_kept, bytes_written


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument(
        "--input",
        type=Path,
        action="append",
        default=None,
        help="raw firmware capture to read; repeat to concatenate several days in order "
             "(default: the DK5EN-98 24.09. field capture alone)",
    )
    parser.add_argument(
        "--start-line",
        type=int,
        default=1,
        help="1-indexed line to start the FIRST --input from (default: 1, i.e. the whole file); "
             "ignored for every --input after the first",
    )
    parser.add_argument(
        "--from-last-reset",
        action="store_true",
        help="start the FIRST --input at its last millis() reset (device reboot) instead of "
             "--start-line -- use for a cold-boot fixture; mutually exclusive with --start-line",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("test/test_nbr_replay/fixtures/dk5en98-20260924.txt"),
        help="fixture file to write (default: test/test_nbr_replay/fixtures/dk5en98-20260924.txt)",
    )
    parser.add_argument(
        "--max-bytes",
        type=int,
        default=2 * 1024 * 1024,
        help="warn (not fail, and never trim content) if the extracted fixture exceeds this "
             "size (default: 2 MiB)",
    )
    args = parser.parse_args(argv)

    if args.from_last_reset and args.start_line != 1:
        print("nbr_replay_extract: --from-last-reset and --start-line are mutually exclusive", file=sys.stderr)
        return 1

    inputs: list[Path] = args.input if args.input else [Path("~/Downloads/dk5en-98-nbr/2026-09-24.log").expanduser()]
    output_path: Path = args.output

    for p in inputs:
        if not p.is_file():
            print(f"nbr_replay_extract: no such file: {p}", file=sys.stderr)
            return 1

    first_start_line = args.start_line
    if args.from_last_reset:
        reset_line = find_last_reset_line(inputs[0])
        if reset_line is None:
            print(f"nbr_replay_extract: {inputs[0]} has no millis() reset -- nothing to start from", file=sys.stderr)
            return 1
        first_start_line = reset_line
        print(f"nbr_replay_extract: last millis() reset in {inputs[0]} is at line {reset_line}")

    total_kept = 0
    for i, p in enumerate(inputs):
        start_line = first_start_line if i == 0 else 1
        lines_read, lines_kept, bytes_written = extract(p, output_path, start_line=start_line, append=(i > 0))
        total_kept += lines_kept
        print(
            f"nbr_replay_extract: {p} (from line {start_line}) -> {output_path}: "
            f"{lines_kept}/{lines_read} lines kept, {bytes_written} bytes total so far"
        )

    final_bytes = output_path.stat().st_size
    print(f"nbr_replay_extract: done: {total_kept} lines kept, {final_bytes} bytes in {output_path}")
    if final_bytes > args.max_bytes:
        print(
            f"nbr_replay_extract: WARNING: fixture is {final_bytes} bytes, "
            f"over the {args.max_bytes}-byte target (content left untrimmed)",
            file=sys.stderr,
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
