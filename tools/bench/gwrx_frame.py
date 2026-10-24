"""Build a MeshCom LoRa text frame (type 0x3A) as encodeAPRS() lays it out.

Usage: python3 -I gwrx_frame.py <msg_id_hex> <src> <dest> <text>   -> prints hex
       python3 -I gwrx_frame.py --selftest                          -> rebuilds corpus f004
"""

from __future__ import annotations

import sys


def build(
    msg_id: int,
    flags: int,
    path: str,
    dest: str,
    text: str,
    hw: int = 0x00,
    mod: int = 0x88,
    fw: int = 0x00,
    last_hw: int = 0xAB,
    sub: int = 0x23,
) -> bytes:
    b = bytearray([0x3A])
    b += msg_id.to_bytes(4, "little")
    b.append(flags)
    b += f"{path}>{dest}:{text}".encode("ascii")
    b += bytes([0x00, hw, mod])
    fcs = sum(b) & 0xFFFF
    b += fcs.to_bytes(2, "big")
    b += bytes([fw, last_hw, sub, 0x7E])
    return bytes(b)


# corpus frame f004 (test/golden/corpus/lora/04-f004.hex), own callsigns
GOLDEN_F004 = (
    "3AA423886AA2444B35454E2D33332C444B35454E2D39383E2A3A7B4345547D323032362D30382D3231"
    "2031303A35383A35380000880D5C00AB237E"
)


def selftest() -> None:
    got = (
        build(0x6A8823A4, 0xA2, "DK5EN-33,DK5EN-98", "*", "{CET}2026-08-21 10:58:58").hex().upper()
    )
    assert got == GOLDEN_F004, got
    print("selftest ok")


def main(argv: list[str]) -> int:
    if argv == ["--selftest"]:
        selftest()
        return 0
    if len(argv) != 4:
        print(__doc__)
        return 2
    mid, src, dest, text = argv
    print(build(int(mid, 16), 0xA2, src, dest, text).hex())
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
