#!/usr/bin/env python3
"""Gate over the committed captures: no golden may contain a broadcast frame.

The BLE corpus of 2026-09-11 made four bench nodes transmit to `*` on the live
MeshCom network, and the captures of that run were committed before anyone
noticed. This check exists so the repository itself refuses to be green while a
poisoned baseline is in it -- a capture containing `*` frames is not merely
embarrassing, it is a baseline a later session would diff G1 against and treat
the broadcast as expected behaviour.

It walks every `ble-frames.bin` under `test/golden/hw/`, derives the node's own
callsign from the directory name, and fails on any own-originated frame
addressed to `*`. Frames relayed from other stations are not ours and are not
flagged.

    python3 test/golden/verify_captures.py
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "bench"))
sys.path.insert(0, str(ROOT / "test" / "golden"))

# Directory name -> the callsign that node originates as.
NODE_CALLSIGN = {
    "heltec-93": "DK5EN-93",
    "rak-90": "DK5EN-90",
    "t-beam-92": "DK5EN-92",
    "t-deck-14": "DK5EN-14",
}


def frames_of(path: Path):
    data = path.read_bytes()
    out, i = [], 0
    while i + 2 <= len(data):
        (n,) = struct.unpack_from("<H", data, i)
        i += 2
        out.append((0.0, data[i:i + n]))
        i += n
    return out


def main() -> int:
    import ble_golden

    problems = []
    checked = 0
    for path in sorted((ROOT / "test" / "golden" / "hw").rglob("ble-frames.bin")):
        node = path.parent.name
        call = NODE_CALLSIGN.get(node)
        if call is None:
            problems.append(f"{path}: unknown node {node!r}, add it to NODE_CALLSIGN")
            continue
        checked += 1
        for finding in ble_golden.verify_no_broadcast(frames_of(path), (call,)):
            problems.append(f"{path}: {finding.strip()}")

    for p in problems:
        print(p, file=sys.stderr)
    print(f"verify_captures: {checked} capture(s) checked, {len(problems)} problem(s)",
          file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    raise SystemExit(main())
