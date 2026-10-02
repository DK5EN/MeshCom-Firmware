#!/usr/bin/env python3
"""Extract the fixture for test/test_topo_shadow from raw firmware log(s).

MeshCom 5 topology, wave 3 (docs/meshcom5-campaign.md), brief W3b. Operator
decision 2026-09-25 replaces the planned 24-h live shadow with a REPLAY
shadow: the DK5EN-98 capture 21.-24.09.2026 drives both the old MHeard path
(updateMheard()/updateHeyPath()/getMheardCount(), src/mheard_functions.cpp)
and the new topology path (nbrNoteFrame()/nbrNoteDirect()/nbrNoteNcnt(),
src/nbr_matrix.cpp) side by side, one frame at a time, and the two are
compared every simulated minute (test/test_topo_shadow/test_topo_shadow.cpp).

This tool needs only ONE kind of raw-log line: "[LOG] ..." (printBuffer_aprs()
in src/loop_functions.cpp, tail from setlogFormatRxTail() in
src/setlog_lines.cpp) -- the decoded frame itself (path, type, RSSI/SNR, HW/
MOD, FCS, the on-device millis() trail "t=<ms>"), WITH the logger's own
leading wall-clock timestamp ("YYYY-MM-DD HH:MM:SS.mmm"), because the shadow
test drives the OLD path's date/time fields from that logger timestamp (see
test_topo_shadow.cpp's file header) rather than from the device's own,
possibly-unsynced clock. Unlike tools/nbr_replay_extract.py (which this tool
otherwise mirrors -- read-only model for this wave, not reused directly since
it also keeps "[NBR]" lines this test does not need), the leading logger
timestamp is therefore kept, not the marker text after "[LOG] " alone: the
regex below intentionally matches from the START of the line.

Every "[LOG] ..." line is kept unconditionally, including one whose trailer
reads "FCS:0000" (decodeAPRS() failed) -- the shadow test replays those too,
into NEITHER side, exactly as OnRxDone() does (msg_type_b_lora == 0x00 exits
before the matrix or MHeard). Dropping them here would silently change which
frames the replay ever sees.

A device reboot is not marked separately: test_topo_shadow.cpp detects it the
same way tools/nbr_replay_extract.py's --from-last-reset does -- a "t=<ms>"
value smaller than the previous one seen in the (possibly multi-day,
concatenated) stream. This tool therefore does no reset bookkeeping of its
own; it only concatenates inputs in the order given.

Usage (one day):
    python3 tools/topo_shadow_extract.py \\
        --input ~/Downloads/dk5en-98-nbr/2026-09-24.log \\
        --output test/test_topo_shadow/fixtures/dk5en98-20260924.txt

Usage (the whole 21.-24.09. window, one fixture, chronological order):
    python3 tools/topo_shadow_extract.py \\
        --input ~/Downloads/dk5en-98-nbr/2026-09-21.log \\
        --input ~/Downloads/dk5en-98-nbr/2026-09-22.log \\
        --input ~/Downloads/dk5en-98-nbr/2026-09-23.log \\
        --input ~/Downloads/dk5en-98-nbr/2026-09-24.log \\
        --output test/test_topo_shadow/fixtures/dk5en98-20260921-24.txt

--input may be repeated; files are concatenated in the order given (each read
from its own line 1 -- there is no --start-line/--from-last-reset here,
unlike nbr_replay_extract.py: this tool keeps the WHOLE window, reboots and
all, rather than starting at one cold boot).
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# A "[LOG]" frame line as printBuffer_aprs() formats it (src/loop_functions.cpp),
# WITH the logger's leading wall-clock timestamp kept (see module docstring):
#   "<logger ts>  <node HH:MM:SS> [LOG] %s %s %03i %c x%08X H%02X S%i T%i M%02X
#    %s>%s%c%s HW:%02i MOD:%01X/%01i FCS:%04X FW:%02i:%c LH:%02X%s"
# with tail = setlogFormatRxTail() -> " RSSI:%d SNR:%d DUP:%c OWN:%c t=%lu"
# (src/setlog_lines.cpp). The path/payload in the middle is free text and is
# deliberately NOT matched -- only the fixed head and the fixed tail, so a
# payload that happens to contain e.g. "H01" or "RSSI:" cannot defeat the
# match (same reasoning as tools/nbr_replay_extract.py's LOG_LINE_RE).
LOG_LINE_RE = re.compile(
    r"\[LOG\] \d+ [!@:] x[0-9A-Fa-f]{8} H[0-9A-Fa-f]+ S\d+ T\d+ M\d+ .*"
    r"RSSI:-?\d+ SNR:-?\d+ DUP:[a-z] OWN:[a-z-] t=\d+\s*$"
)


def should_keep(line: str) -> bool:
    return bool(LOG_LINE_RE.search(line))


def extract(input_path: Path, output_path: Path, append: bool) -> tuple[int, int, int]:
    """Copy the matching lines from input_path to output_path (overwritten
    unless append=True).

    Returns (lines_read, lines_kept, bytes_now_in_output).
    """
    lines_read = 0
    lines_kept = 0

    output_path.parent.mkdir(parents=True, exist_ok=True)

    mode = "a" if append else "w"
    with input_path.open("r", encoding="utf-8", errors="replace") as src, \
         output_path.open(mode, encoding="utf-8", newline="\n") as dst:
        for line in src:
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
        required=True,
        help="raw firmware capture to read; repeat to concatenate several days in order",
    )
    parser.add_argument(
        "--output",
        type=Path,
        required=True,
        help="fixture file to write",
    )
    parser.add_argument(
        "--max-bytes",
        type=int,
        default=3 * 1024 * 1024,
        help="warn (not fail, and never trim content) if the extracted fixture exceeds this "
             "size (default: 3 MiB -- the campaign's checked-in-fixture ceiling)",
    )
    args = parser.parse_args(argv)

    inputs: list[Path] = args.input
    output_path: Path = args.output

    for p in inputs:
        if not p.is_file():
            print(f"topo_shadow_extract: no such file: {p}", file=sys.stderr)
            return 1

    total_kept = 0
    for i, p in enumerate(inputs):
        lines_read, lines_kept, bytes_written = extract(p, output_path, append=(i > 0))
        total_kept += lines_kept
        print(
            f"topo_shadow_extract: {p} -> {output_path}: "
            f"{lines_kept}/{lines_read} lines kept, {bytes_written} bytes total so far"
        )

    final_bytes = output_path.stat().st_size
    print(f"topo_shadow_extract: done: {total_kept} lines kept, {final_bytes} bytes in {output_path}")
    if final_bytes > args.max_bytes:
        print(
            f"topo_shadow_extract: WARNING: fixture is {final_bytes} bytes, "
            f"over the {args.max_bytes}-byte target (content left untrimmed) -- "
            "check in a smaller slice instead and let the test read the rest from "
            "~/Downloads/dk5en-98-nbr at runtime (skip, not fail, when absent)",
            file=sys.stderr,
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
