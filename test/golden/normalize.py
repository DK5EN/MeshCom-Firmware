#!/usr/bin/env python3
"""Normalizer for MeshCom golden captures (test plan section 7).

Golden captures are compared before and after a refactor. A raw capture never
compares equal to itself: it carries wall-clock stamps, msg_ids derived from
millis(), RF readings, heap figures and addresses. This module removes exactly
those fields and nothing else, so that every surviving difference is a real
behavioural difference.

Design rules, all of them load-bearing:

- **Substitute, never delete.** A line whose only content was volatile still
  has to appear, with `<TS>` / `<NUM>` / `<RF>` in place of the value. A missing
  line is a real diff and must stay visible as one.
- **Line order is preserved.** Reordering is a behavioural difference.
- **msg_ids become a per-capture sequence**, not a constant: the first distinct
  id seen is `<ID1>`, the next `<ID2>`. Two captures agree only if the *pattern*
  of reuse agrees, which is what the dedup and ack paths are judged on.
- **Firmware version strings are kept.** They change deliberately, at a release.

Used for the hardware goldens G0/G1/G2 (test plan section 6) and by the
twin-diff fixtures. Binary captures (BLE frames, UDP datagrams) do not go
through the text path; use `mask_binary()` with the per-surface offset list.

    python3 test/golden/normalize.py capture.txt > capture.norm.txt
    python3 test/golden/normalize.py --in-place test/golden/hw/G0/rak-90/*.txt
    cat capture.txt | python3 test/golden/normalize.py
    python3 test/golden/normalize.py --self-test

No third-party dependencies: stdlib + type hints only.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Dict, Iterable, List, Sequence, Tuple

# --------------------------------------------------------------- rules
#
# Order matters: the earlier a rule fires, the fewer characters remain for the
# later ones to misread. Time before numbers, ids before generic hex.

# Host-side prefix written by tools/bench/serial_session.py: "23:17:27 " or
# "23:05:11.568 ". Firmware-side stamps look the same but sit mid-line
# ("[HEAP] 22:06:08 ...", "22:06:24 Insert own_msg_id:...") or carry a
# semicolon ("00:00:00;[HEAP];...").
_TIME = r"\d{2}:\d{2}:\d{2}(?:\.\d{1,3})?"

_RULES: List[Tuple[re.Pattern[str], str]] = [
    # Dates in every spelling the firmware and the tools emit.
    (re.compile(r"\b\d{4}-\d{2}-\d{2}\b"), "<TS>"),
    (re.compile(r"\b\d{2}\.\d{2}\.\d{2,4}\b"), "<TS>"),
    (re.compile(r"\b\d{2}\.\d{2}\.\d{2}\s+" + _TIME), "<TS>"),
    # Clock stamps, wherever they appear.
    (re.compile(r"\b" + _TIME + r"\b"), "<TS>"),
    # RF readings. The label is kept, the number goes. The label appears both
    # as console text ("RSSI:\t\t-45 dBm") and as a JSON key ("rssi":-105), so
    # the optional quote and the [:=] alternative are both needed.
    (re.compile(r"(\brssi\"?\s*[:=]\s*)-?\d+(\.\d+)?", re.I), r"\1<RF>"),
    (re.compile(r"(\bsnr\"?\s*[:=]\s*)-?\d+(\.\d+)?", re.I), r"\1<RF>"),
    (re.compile(r"(Frequency error:?\s*)-?\d+(\.\d+)?(\s*Hz)?", re.I), r"\1<RF>\3"),
    # Addresses. MAC/BSSID before IPv4, or the colon form eats the wrong thing.
    (re.compile(r"\b(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}\b"), "<ADDR>"),
    (re.compile(r"\b(?:\d{1,3}\.){3}\d{1,3}\b"), "<ADDR>"),
    # Heap monitor: "[HEAP] <TS> 141960 137660 131060 (mon)" -- three figures,
    # and the "(init)"/"(mon)" tag is behaviour, so it stays.
    # Two spellings are in the field: "[HEAP] <TS> a b c (mon)" (space form,
    # stamp after the tag) and "<TS>;[HEAP];a;b;c;(init)" (semicolon form,
    # stamp before it). The optional group covers both.
    (re.compile(r"(\[HEAP\][;\s]+(?:<TS>[;\s]+)?)\d+([;\s]+)\d+([;\s]+)\d+"),
     r"\1<NUM>\2<NUM>\3<NUM>"),
    # Uptime, stack high-water marks, free-heap one-offs.
    (re.compile(r"\b(uptime|Uptime|watermark|highwater|freeheap|heap)([:=\s]+)\d+"),
     r"\1\2<NUM>"),
    # Durations and ages from the semicolon-instrumented lines ([ETH];...;ms;N,
    # [EXT];tx;...;stack_hwm;N) and from [INSTR-LOOP], which writes the same
    # keys space-separated. Every one of these is a clock reading, not a
    # decision, so the key stays and the figure goes.
    (re.compile(r"\b(ms|stack_hwm|section_ms|sections_ms|link_age_s|hb_age_s"
                r"|rx_max_ms|tx_max_ms)([;=\s])\d+"), r"\1\2<NUM>"),
    # The INSTRUMENT_ENABLED profiler lines ([INSTR-LOOP], [INSTR-SECT]) are
    # pure measurement: every microsecond figure and the iteration count vary
    # per run. They appear only in the instrument capture (test plan H3).
    (re.compile(r"\b(\w*_us)([;=\s])\d+"), r"\1\2<NUM>"),
    (re.compile(r"(\[INSTR-[A-Z]+\][^\n]*?\bn)(\s+)\d+"), r"\1\2<NUM>"),
    (re.compile(r"\b(int_free|int_min|int_largest|psram_free|psram_largest)"
                r"(\s+)-?\d+"), r"\1\2<NUM>"),
]

# msg_ids. Every spelling the firmware uses for the same 32-bit value.
_MSGID_RULES: List[re.Pattern[str]] = [
    re.compile(r"(?<=msg_id=)([0-9A-Fa-f]{8})"),
    re.compile(r"(?<=own_msg_id:)([0-9A-Fa-f]{8})"),
    re.compile(r"(?<=<)([0-9A-Fa-f]{8})(?=>)"),
    re.compile(r"(?<=MSG-ID:)([0-9A-Fa-f]{8})"),
    # EXTUDP JSON carries the same value as a quoted string.
    re.compile(r"(?<=\"msg_id\":\")([0-9A-Fa-f]{8})"),
]


class Normalizer:
    """Stateful because msg_ids are numbered in order of first appearance."""

    def __init__(self) -> None:
        self._ids: Dict[str, str] = {}

    def _msgid(self, raw: str) -> str:
        key = raw.upper()
        if key not in self._ids:
            self._ids[key] = f"<ID{len(self._ids) + 1}>"
        return self._ids[key]

    def line(self, text: str) -> str:
        for pattern in _MSGID_RULES:
            text = pattern.sub(lambda m: self._msgid(m.group(1)), text)
        for pattern, repl in _RULES:
            text = pattern.sub(repl, text)
        return text

    def text(self, blob: str) -> str:
        # splitlines(keepends) would let a lone CR split a line; the captures
        # are read as text with universal newlines, so \n is the only separator
        # that survives to here.
        return "".join(self.line(ln) + "\n" for ln in blob.split("\n")[:-1]) + \
            (self.line(blob.split("\n")[-1]) if not blob.endswith("\n") else "")

    @property
    def id_map(self) -> Dict[str, str]:
        """Original msg_id -> placeholder, for reports that need the mapping."""
        return dict(self._ids)


def normalize_text(blob: str) -> str:
    """One-shot convenience; a fresh id sequence per call."""
    return Normalizer().text(blob)


def mask_binary(data: bytes, spans: Sequence[Tuple[int, int]], fill: int = 0x00) -> bytes:
    """Zero the volatile byte ranges of one binary frame.

    `spans` are (offset, length) pairs in frame-local coordinates, taken from
    the wire-format document for that surface -- e.g. the four timestamp bytes
    and the msg_id of a 0x40 BLE notification. Offsets past the end of the
    frame are ignored rather than raising: short frames are legal on every
    surface and a truncated frame must stay visible as a length difference,
    not disappear behind an exception.
    """
    out = bytearray(data)
    for offset, length in spans:
        if offset >= len(out):
            continue
        end = min(offset + length, len(out))
        for i in range(offset, end):
            out[i] = fill
    return bytes(out)


# --------------------------------------------------------------- self-test

_SELF_TEST: List[Tuple[str, str]] = [
    ("23:17:27 [LoRa]...RF_SF: 11",
     "<TS> [LoRa]...RF_SF: 11"),
    ("23:05:47.484 [LoRa]...Received packet: RSSI:\t\t-45 dBm / SNR:\t\t6 dB / "
     "Frequency error:\t-18.41 Hz",
     "<TS> [LoRa]...Received packet: RSSI:\t\t<RF> dBm / SNR:\t\t<RF> dB / "
     "Frequency error:\t<RF> Hz"),
    ("23:06:10.566 [HEAP] 22:06:08 141960 137660 131060 (mon)",
     "<TS> [HEAP] <TS> <NUM> <NUM> <NUM> (mon)"),
    ("23:17:38 00:00:00;[HEAP];136484;136504;68242;(init)",
     "<TS> <TS>;[HEAP];<NUM>;<NUM>;<NUM>;(init)"),
    ("[WIFI]...SSID: ORBI63 CHAN: 3 RSSI: -49 BSSID: 5A:AF:97:2E:2B:8B",
     "[WIFI]...SSID: ORBI63 CHAN: 3 RSSI: <RF> BSSID: <ADDR>"),
    ("Ethernet.localIP(): 192.168.68.72",
     "Ethernet.localIP(): <ADDR>"),
    ("[INIT]...FLASH layout 20260724 ok, build 20260905",
     "[INIT]...FLASH layout 20260724 ok, build 20260905"),
    ("[EXT];tx;len;266;stack_hwm;153;ms;137674",
     "[EXT];tx;len;266;stack_hwm;<NUM>;ms;<NUM>"),
    ("[INSTR-LOOP] gap ms 3329 in unattributed section_ms 0 sections_ms 0",
     "[INSTR-LOOP] gap ms <NUM> in unattributed section_ms <NUM> sections_ms <NUM>"),
    ("[INSTR-SECT] lora_sm n 13 total_us 24415 avg_us 1878 max_us 5860",
     "[INSTR-SECT] lora_sm n <NUM> total_us <NUM> avg_us <NUM> max_us <NUM>"),
    ("[INSTR-HEAP] instr int_free 109744 int_min -1 int_largest 96392 psram_free 0",
     "[INSTR-HEAP] instr int_free <NUM> int_min <NUM> int_largest <NUM> psram_free <NUM>"),
    # Payload content that must survive: coordinates, altitude, frame length.
    ('[EXT] Out: {"lat":48.4228,"alt":1535} Len: 274',
     '[EXT] Out: {"lat":48.4228,"alt":1535} Len: 274'),
    ('[EXT] Out: {"src":"DD7MH-55","msg_id":"3BAC5383","rssi":-105,"snr":-18} Len: 266',
     '[EXT] Out: {"src":"DD7MH-55","msg_id":"<ID1>","rssi":<RF>,"snr":<RF>} Len: 266'),
]

_SELF_TEST_SEQ: Tuple[str, str] = (
    # Same id twice, then a new one: the reuse pattern is what is compared.
    "[MC-DBG] RX_DEDUP_NEW msg_id=4106E0A1\n"
    "[MC-DBG] RX_DEDUP_DUP msg_id=4106E0A1 slot=0\n"
    "22:06:24 Insert own_msg_id:EA25A137 <EA25A137>\n",
    "[MC-DBG] RX_DEDUP_NEW msg_id=<ID1>\n"
    "[MC-DBG] RX_DEDUP_DUP msg_id=<ID1> slot=0\n"
    "<TS> Insert own_msg_id:<ID2> <<ID2>>\n",
)


def self_test() -> int:
    failures = 0
    for raw, want in _SELF_TEST:
        got = Normalizer().line(raw)
        if got != want:
            failures += 1
            print(f"FAIL line\n  in   {raw!r}\n  want {want!r}\n  got  {got!r}",
                  file=sys.stderr)
    raw, want = _SELF_TEST_SEQ
    got = Normalizer().text(raw)
    if got != want:
        failures += 1
        print(f"FAIL sequence\n  want {want!r}\n  got  {got!r}", file=sys.stderr)

    masked = mask_binary(bytes(range(10)), [(2, 4), (9, 8)])
    if masked != bytes([0, 1, 0, 0, 0, 0, 6, 7, 8, 0]):
        failures += 1
        print(f"FAIL mask_binary: {masked!r}", file=sys.stderr)

    # Idempotence: normalizing a normalized capture must not move it again,
    # or a re-run of the compare script would report a false diff.
    for raw, _ in _SELF_TEST:
        once = Normalizer().line(raw)
        if Normalizer().line(once) != once:
            failures += 1
            print(f"FAIL idempotence: {once!r}", file=sys.stderr)

    print("normalize.py self-test: "
          + ("ok" if failures == 0 else f"{failures} failure(s)"), file=sys.stderr)
    return 1 if failures else 0


# --------------------------------------------------------------- cli


def main(argv: Iterable[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("files", nargs="*", type=Path,
                    help="capture files; stdin is read when none are given")
    ap.add_argument("--in-place", action="store_true",
                    help="rewrite each file instead of writing to stdout")
    ap.add_argument("--self-test", action="store_true", help="run the built-in tests")
    args = ap.parse_args(list(argv) if argv is not None else None)

    if args.self_test:
        return self_test()

    if not args.files:
        if args.in_place:
            ap.error("--in-place needs file arguments")
        sys.stdout.write(normalize_text(sys.stdin.read()))
        return 0

    for path in args.files:
        out = normalize_text(path.read_text(errors="replace"))
        if args.in_place:
            path.write_text(out)
        else:
            sys.stdout.write(out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
