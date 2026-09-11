#!/usr/bin/env python3
"""Parse, rebuild and rewrite MeshCom LoRa frames (wire format doc 11 section 1).

One implementation of the frame layout for every golden-capture tool, so that
the corpus builder, the corpus lint and any future replay helper cannot drift
apart -- which is the failure this whole campaign exists to remove.

Layout (doc 11 section 1.1), all offsets after the fixed head are variable:

    0      1   payload type: 0x3A ':' text | 0x21 '!' position | 0x40 '@' hey/wx
    1      4   msg_id, uint32 little-endian
    5      1   flags + hop nibble
    6      n   source path, ASCII, terminated by '>'
    ..     n   destination, ASCII, terminated by a REPEAT of the type byte
    ..     n   payload, ASCII, terminated by 0x00
    ..     1   HW id of the originator
    ..     1   MOD byte: (modulation & 0x0F) | (country << 4)
    ..     2   FCS, big-endian byte sum over everything up to and incl. MOD
    ..     n   trailer: FW, LASTHW, FW-sub, 0x7E -- parsed "if present"

Two frame shapes are deliberately *not* parsed here and come back as None:

- the compact 12-byte binary ack (type 0x41, doc 11 section 1.5) has no path
  and no FCS at all;
- anything shorter than the fixed head, or without the '>' path terminator.

`None` means "not a path-bearing frame", not "corrupt": the corpus contains
both, and a caller that treats one as the other would either skip a frame that
needs rewriting or try to rewrite one that has no callsign in it.

Verified against all 13 frames of `test/test_aprs_corpus/corpus.txt`: the FCS
this module computes equals the sender's on all 12 path-bearing frames.

No third-party dependencies: stdlib + type hints only.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Dict, List, Optional

MSG_TYPE_ACK = 0x41
PATH_SEP = b">"
PATH_DELIM = ","
FRAME_MIN_LEN = 16  # decodeAPRS() rejects anything shorter


@dataclass
class Frame:
    """One path-bearing LoRa frame, split at its field boundaries."""

    type: int
    msg_id: int
    flags: int
    path: List[str]
    dest: bytes
    payload: bytes
    hw: int
    mod: int
    fcs: int           # as it stood in the parsed bytes; encode() recomputes
    trailer: bytes     # FW, LASTHW, FW-sub, 0x7E if the sender wrote them

    def head(self) -> bytes:
        """Everything the FCS is computed over: offset 0 through the MOD byte."""
        return (
            bytes([self.type])
            + self.msg_id.to_bytes(4, "little")
            + bytes([self.flags])
            + PATH_DELIM.join(self.path).encode("ascii")
            + PATH_SEP
            + self.dest
            + bytes([self.type])
            + self.payload
            + b"\x00"
            + bytes([self.hw, self.mod])
        )

    def compute_fcs(self) -> int:
        return sum(self.head()) & 0xFFFF

    def encode(self) -> bytes:
        """Rebuild the frame with a freshly computed FCS.

        Always recomputed, never copied from `fcs`: the only reason to rebuild
        a frame is that something in it changed, and a stale checksum is
        exactly the bug a gateway would not catch -- it drops the frame
        silently.
        """
        return self.head() + self.compute_fcs().to_bytes(2, "big") + self.trailer


def parse(raw: bytes) -> Optional[Frame]:
    """Split one raw frame, or return None if it carries no source path."""
    if len(raw) < FRAME_MIN_LEN or raw[0] == MSG_TYPE_ACK:
        return None

    ftype = raw[0]
    end = raw.find(PATH_SEP, 6)
    if end < 0:
        return None
    path = raw[6:end].decode("ascii", errors="replace").split(PATH_DELIM)

    i = end + 1
    dest_end = raw.find(bytes([ftype]), i)
    if dest_end < 0:
        return None
    dest = raw[i:dest_end]

    i = dest_end + 1
    payload_end = raw.find(b"\x00", i)
    if payload_end < 0:
        return None
    payload = raw[i:payload_end]

    i = payload_end + 1
    if i + 4 > len(raw):
        return None

    return Frame(
        type=ftype,
        msg_id=int.from_bytes(raw[1:5], "little"),
        flags=raw[5],
        path=path,
        dest=dest,
        payload=payload,
        hw=raw[i],
        mod=raw[i + 1],
        fcs=int.from_bytes(raw[i + 2:i + 4], "big"),
        trailer=raw[i + 4:],
    )


def callsigns(raw: bytes) -> List[str]:
    """Every callsign in the frame's source path; empty for non-path frames."""
    frame = parse(raw)
    return list(frame.path) if frame else []


def rewrite_path(raw: bytes, mapping: Dict[str, str]) -> bytes:
    """Replace path callsigns via `mapping` and rebuild with a correct FCS.

    A frame whose path this changes is a different frame on the air, so the
    FCS has to move with it. Callsigns not in the mapping are left alone;
    a frame with no path comes back unchanged.
    """
    frame = parse(raw)
    if frame is None:
        return raw
    if not any(call in mapping for call in frame.path):
        return raw
    frame.path = [mapping.get(call, call) for call in frame.path]
    return frame.encode()


def _self_test() -> int:
    """Round-trip and FCS check against the committed on-air corpus."""
    from pathlib import Path

    corpus = Path(__file__).resolve().parents[2] / "test" / "test_aprs_corpus" / "corpus.txt"
    failures = 0
    parsed = 0
    for line in corpus.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        name, hexstr = line.split()
        raw = bytes.fromhex(hexstr)
        frame = parse(raw)
        if frame is None:
            continue
        parsed += 1
        if frame.compute_fcs() != frame.fcs:
            failures += 1
            print(f"FAIL {name}: fcs {frame.fcs:#06x} != computed "
                  f"{frame.compute_fcs():#06x}")
        if frame.encode() != raw:
            failures += 1
            print(f"FAIL {name}: re-encode is not byte-identical")

    # A rewrite must change the bytes and still verify.
    sample = None
    for line in corpus.read_text().splitlines():
        if line.startswith("f001"):
            sample = bytes.fromhex(line.split()[1])
    assert sample is not None
    out = rewrite_path(sample, {"DL2JA-1": "DK5EN-1", "DL2JA-2": "DK5EN-2"})
    if out == sample:
        failures += 1
        print("FAIL rewrite_path: bytes unchanged")
    rewritten = parse(out)
    if rewritten is None or rewritten.path != ["DK5EN-1", "DK5EN-2"]:
        failures += 1
        print("FAIL rewrite_path: path not applied")
    elif rewritten.compute_fcs() != rewritten.fcs:
        failures += 1
        print("FAIL rewrite_path: FCS not recomputed")

    print(f"mc_frame.py self-test: {parsed} frames parsed, "
          + ("ok" if failures == 0 else f"{failures} failure(s)"))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(_self_test())
