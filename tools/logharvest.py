#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
"""Harvest test corpora from MeshCom firmware serial logs.

The firmware logs three things this tool can turn back into bytes:

1. ``[MC-DBG] CRC_PAYLOAD[n]:`` — a raw hex dump of a frame the radio
   rejected on CRC (``src/esp32/esp32_main.cpp:3977``, ESP32 only).  These are
   byte-exact and become a fuzz corpus for ``decodeAPRS()``.

2. ACK frame lines from ``printBuffer_ack()`` (``src/loop_functions.cpp:3073``)
   — every byte of the 7- or 12-byte frame is printed, so the frame is
   byte-exact too.  Two firmware generations print different layouts; both are
   recognised.

3. Decoded frame lines from ``printBuffer_aprs()``
   (``src/loop_functions.cpp:3066``) — the *parsed* field set, not the wire
   bytes.  These become re-encode vectors: ``encodeAPRS()`` rebuilds the frame
   from the fields, and the rebuilt length and byte-sum are compared against
   the values the sender put on the air.  ``msg_fcs`` is read from the wire
   (``aprs_functions.cpp:416``) and was validated before the frame was logged,
   so that comparison is not circular.

Usage::

    uv run tools/logharvest.py ~/Downloads/dg0opk ~/Downloads/dj8meh

Only ``MH-LoRa``/``RX-LoRa2``/``[LOG]`` records are usable as an oracle.
``TX-LoRa`` records were produced by our own encoder and are emitted with a
``tx`` tag so the consumer can keep them out of the oracle.
"""

from __future__ import annotations

import argparse
import re
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

# ---------------------------------------------------------------- line prefixes

# serial_monitor.py:  "2026-03-06 15:36:58.248  <text>"
# raw firmware:       "[20689.05h.55m.30s.813] <text>"
_PREFIX_RE = re.compile(
    r"^(?:\[\d+\.\d+h\.\d+m\.\d+s\.\d+\]|\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+)\s*"
)


def strip_prefix(line: str) -> str:
    return _PREFIX_RE.sub("", line.rstrip("\r\n"))


# ---------------------------------------------------------------- CRC dumps

_CRC_PAYLOAD_RE = re.compile(r"CRC_PAYLOAD\[(\d+)\]:\s+((?:[0-9A-F]{2} ?)+)")
_CRC_ERROR_RE = re.compile(
    r"CRC_ERROR rssi=(-?\d+) snr=(-?\d+) freq_err=(-?[\d.]+) size=(\d+) ts=(\d+)"
)


@dataclass(frozen=True)
class CrcFrame:
    hexstr: str
    rssi: int | None
    snr: int | None
    freq_err: float | None


# ---------------------------------------------------------------- capture hook

# capture_functions.cpp, drained in main.cpp's loop(). Byte-exact frames that
# the radio ACCEPTED -- the counterpart to the CRC dumps above, and the only
# source that needs no reconstruction at all.
_CAPTURE_RE = re.compile(
    r"\[MC-TEST\] (RX|TX)_FRAME len=(\d+)(?: rssi=(-?\d+) snr=(-?\d+))? hex=([0-9A-F]+)"
)
# Frames the ring had no room for -- the capture goes lossy exactly when the
# channel is busy, so this number belongs next to any claim of completeness.
_CAPTURE_DROP_RE = re.compile(r"\[MC-TEST\] CAPTURE_DROPPED n=(\d+)")


# ---------------------------------------------------------------- ACK frames

# Current printBuffer_ack(): "%s %s 012 %c x%02X%02X%02X%02X H%02X x%02X%02X%02X%02X %02X %02X"
# payload[0] as a character, then p[4..1], p[5], p[9..6], p[10], p[11].
_ACK12_NEW_RE = re.compile(
    r"\d\d:\d\d:\d\d (\S+)\s+012 (.) x([0-9A-F]{8}) H([0-9A-F]{2}) "
    r"x([0-9A-F]{8}) ([0-9A-F]{2}) ([0-9A-F]{2})\s*$"
)
_ACK7_NEW_RE = re.compile(
    r"\d\d:\d\d:\d\d (\S+)\s+007 (.) x([0-9A-F]{8}) H([0-9A-F]{2}) ([0-9A-F]{2})\s*$"
)

# Pre-4.35p printBuffer_ack(): "%s %s: %02X %02X%02X%02X%02X %02X %02X%02X%02X%02X %02X %02X"
# Same byte mapping, type printed as hex and a ':' after the source label.
_ACK12_OLD_RE = re.compile(
    r"\d\d:\d\d:\d\d (\S+): ([0-9A-F]{2}) ([0-9A-F]{8}) ([0-9A-F]{2}) "
    r"([0-9A-F]{8}) ([0-9A-F]{2}) ([0-9A-F]{2})\s*$"
)
_ACK7_OLD_RE = re.compile(
    r"\d\d:\d\d:\d\d (\S+): ([0-9A-F]{2}) ([0-9A-F]{8}) ([0-9A-F]{2}) ([0-9A-F]{2})\s*$"
)


def _ack12(type_byte: int, own_id: str, byte5: str, acked_id: str, b10: str, b11: str) -> str:
    """Rebuild the 12 wire bytes.  The log prints both 32-bit fields MSB-first
    while the wire carries them little-endian, so each is reversed here."""
    own = bytes.fromhex(own_id)[::-1]
    acked = bytes.fromhex(acked_id)[::-1]
    return (
        bytes([type_byte]) + own + bytes.fromhex(byte5) + acked + bytes.fromhex(b10 + b11)
    ).hex().upper()


def _ack7(type_byte: int, own_id: str, byte5: str, b6: str) -> str:
    own = bytes.fromhex(own_id)[::-1]
    return (bytes([type_byte]) + own + bytes.fromhex(byte5 + b6)).hex().upper()


def parse_ack(text: str) -> tuple[str, str] | None:
    """Return (source_label, hex) for an ACK log line, else None."""
    if (m := _ACK12_NEW_RE.search(text)) is not None:
        return m.group(1), _ack12(ord(m.group(2)), m.group(3), m.group(4), *m.group(5, 6, 7))
    if (m := _ACK7_NEW_RE.search(text)) is not None:
        return m.group(1), _ack7(ord(m.group(2)), m.group(3), m.group(4), m.group(5))
    if (m := _ACK12_OLD_RE.search(text)) is not None:
        return m.group(1), _ack12(int(m.group(2), 16), m.group(3), m.group(4), *m.group(5, 6, 7))
    if (m := _ACK7_OLD_RE.search(text)) is not None:
        return m.group(1), _ack7(int(m.group(2), 16), m.group(3), m.group(4), m.group(5))
    return None


# ---------------------------------------------------------------- decoded frames

# printBuffer_aprs():
#   "%s %s %03i %c x%08X H%02X S%i T%i M%02X %s>%s%c%s
#    HW:%02i MOD:%01X/%01i FCS:%04X FW:%02i:%c LH:%02X"
# The payload can contain anything printable including " HW:", so the trailer is
# matched right-anchored and the greedy middle group backtracks to the last one.
_APRS_RE = re.compile(
    r"\d\d:\d\d:\d\d (\S+?):?\s+(\d{3}) (.) x([0-9A-F]{8}) H([0-9A-F]{2}) "
    r"S(\d) T(\d) M([0-9A-F]{2}) (.*) "
    r"HW:(\d+) MOD:([0-9A-F])/(\d+) FCS:([0-9A-F]{4}) FW:(\d+):(.) LH:([0-9A-F]{2})\s*$"
)

# Sources that print the frame straight out of decodeAPRS(), before the relay
# path mutates it (lora_functions.cpp:536,539,570,780).  Only these carry a
# wire-derived FCS.
_RX_SOURCES = {"MH-LoRa", "RX-LoRa2", "RX-LoRa", "[LOG]", "RX-UDP"}


@dataclass(frozen=True)
class Vector:
    tag: str  # "rx" (oracle) or "tx" (encoder output, circular)
    payload_type: int
    msg_id: int
    max_hop: int
    server: int
    track: int
    mesh: int
    hw: int
    mod: int
    fcs: int
    fw: int
    last_hw: int
    fw_sub: int
    exp_len: int
    src_path: str
    dst_path: str
    payload: str


def parse_aprs(text: str) -> Vector | None:
    m = _APRS_RE.search(text)
    if m is None:
        return None
    # A byte the serial capture lost to RF/UART noise came in as U+FFFD. Its
    # original value is unrecoverable, and guessing one would silently corrupt
    # the byte-sum the vector exists to check -- drop the whole record.
    if "�" in m.group(0):
        return None
    source = m.group(1)
    ptype = m.group(3)
    middle = m.group(9)

    # lora_functions.cpp:536/539 prints the "[LOG]" line BEFORE checking whether
    # decodeAPRS() returned 0x00, so a rejected frame is logged from a struct
    # that initAPRS() left at payload_type 0 and msg_len = rsize. Those are not
    # frames and must not enter the oracle. 0x41 (ACK) never reaches
    # printBuffer_aprs(); it returns early at aprs_functions.cpp:130.
    if ord(ptype) not in (0x21, 0x3A, 0x40):
        return None

    # middle == "<src>><dst><type><payload>"
    if ">" not in middle:
        return None
    src_path, rest = middle.split(">", 1)
    # The destination is terminated by a repeat of the type byte.
    idx = rest.find(ptype)
    if idx < 0:
        return None
    dst_path, payload = rest[:idx], rest[idx + 1 :]

    mod = (int(m.group(11), 16) << 4) | int(m.group(12))
    return Vector(
        tag="rx" if source in _RX_SOURCES else "tx",
        payload_type=ord(ptype),
        msg_id=int(m.group(4), 16),
        max_hop=int(m.group(5), 16),
        server=int(m.group(6)),
        track=int(m.group(7)),
        mesh=int(m.group(8), 16),
        hw=int(m.group(10)),
        mod=mod,
        fcs=int(m.group(13), 16),
        fw=int(m.group(14)),
        last_hw=int(m.group(16), 16),
        fw_sub=ord(m.group(15)),
        exp_len=int(m.group(2)),
        src_path=src_path,
        dst_path=dst_path,
        payload=payload,
    )


# ---------------------------------------------------------------- harvesting

@dataclass
class Harvest:
    captured: dict[str, tuple[str, int | None, int | None]]  # hex -> (dir, rssi, snr)
    crc: dict[str, CrcFrame]
    acks: dict[str, str]  # hex -> source label
    vectors: dict[tuple, Vector]
    stats: Counter


def harvest(paths: list[Path]) -> Harvest:
    out = Harvest(captured={}, crc={}, acks={}, vectors={}, stats=Counter())
    for path in paths:
        pending_err: tuple[int, int, float] | None = None
        # Serial captures carry stray non-UTF8 bytes from RF/UART noise; the
        # same reason tools/loganalyse.sh normalises to ASCII (TOOL-04).
        with path.open("r", encoding="ascii", errors="replace") as fh:
            for raw in fh:
                out.stats["lines"] += 1
                text = strip_prefix(raw)

                if (m := _CAPTURE_RE.search(text)) is not None:
                    declared, hexstr = int(m.group(2)), m.group(5)
                    out.stats["capture_seen"] += 1
                    if len(hexstr) != declared * 2:
                        out.stats["capture_truncated"] += 1
                        continue
                    rssi = int(m.group(3)) if m.group(3) is not None else None
                    snr = int(m.group(4)) if m.group(4) is not None else None
                    out.captured.setdefault(hexstr, (m.group(1), rssi, snr))
                    continue

                if (m := _CAPTURE_DROP_RE.search(text)) is not None:
                    out.stats["capture_dropped"] += int(m.group(1))
                    continue

                if (m := _CRC_ERROR_RE.search(text)) is not None:
                    pending_err = (int(m.group(1)), int(m.group(2)), float(m.group(3)))
                    continue

                if (m := _CRC_PAYLOAD_RE.search(text)) is not None:
                    declared = int(m.group(1))
                    hexstr = m.group(2).replace(" ", "")
                    out.stats["crc_seen"] += 1
                    # A dump truncated by a full CDC FIFO (printfdeb drops
                    # bytes, see g_cdcTxDropped) is not the frame the radio saw.
                    if len(hexstr) != declared * 2:
                        out.stats["crc_truncated"] += 1
                        pending_err = None
                        continue
                    err = pending_err or (None, None, None)
                    out.crc.setdefault(hexstr, CrcFrame(hexstr, *err))
                    pending_err = None
                    continue

                if (ack := parse_ack(text)) is not None:
                    out.stats["ack_seen"] += 1
                    out.acks.setdefault(ack[1], ack[0])
                    continue

                if _APRS_RE.search(text) is not None:
                    out.stats["aprs_lines"] += 1
                if (vec := parse_aprs(text)) is not None:
                    out.stats["aprs_seen"] += 1
                    out.stats[f"aprs_{vec.tag}"] += 1
                    key = (
                        vec.payload_type, vec.msg_id, vec.max_hop, vec.server, vec.track,
                        vec.mesh, vec.hw, vec.mod, vec.fcs, vec.fw, vec.last_hw,
                        vec.fw_sub, vec.exp_len, vec.src_path, vec.dst_path, vec.payload,
                    )
                    out.vectors.setdefault(key, vec)
    return out


def select_diverse(frames: list[str], cap: int) -> list[str]:
    """Cap the corpus without collapsing its variety.

    Buckets by the first byte -- the type byte decodeAPRS() branches on -- and
    gives every bucket a share of the cap proportional to its size, but never
    less than one slot, so the rare malformed type bytes survive the cut.
    Within a bucket the picks are evenly spaced over the sorted list, which
    makes the selection deterministic and the diff reviewable.
    """
    if len(frames) <= cap:
        return sorted(frames)

    buckets: dict[str, list[str]] = {}
    for f in sorted(frames):
        buckets.setdefault(f[:2], []).append(f)

    keys = sorted(buckets)
    total = len(frames)
    # Proportional share, floor-rounded, with one guaranteed slot per bucket.
    quota = {k: max(1, int(cap * len(buckets[k]) / total)) for k in keys}
    # Hand the rounding remainder to the largest buckets.
    for k in sorted(keys, key=lambda k: -len(buckets[k])):
        if sum(quota.values()) >= cap:
            break
        quota[k] += 1

    picked: list[str] = []
    for k in keys:
        b, n = buckets[k], min(quota[k], len(buckets[k]))
        step = len(b) / n
        picked.extend(b[int(i * step)] for i in range(n))
    return sorted(set(picked))[:cap]


def _signature(v: Vector) -> tuple:
    """What makes a vector behaviourally distinct for the re-encode check.

    The msg_id and the exact payload text do not change how encodeAPRS() lays
    out the frame; the field shapes around them do.  Collapsing on this keeps
    every distinct encoder path while discarding the ~15x redundancy that comes
    from the same beacon being logged again every few minutes.
    """
    return (
        v.tag, v.payload_type, v.max_hop, v.server, v.track, v.mesh,
        v.hw, v.mod, v.fw, v.last_hw, v.fw_sub,
        v.dst_path,
        v.src_path.count(",") + 1,      # path element count
        len(v.src_path),
        len(v.payload) // 16,           # payload length bucket
        v.exp_len - len(v.payload),     # framing overhead: catches short trailers
    )


def select_vectors(vecs: list[Vector], cap: int) -> list[Vector]:
    """One representative per behavioural signature, capped.

    When the cap bites, rare signatures win: a behaviour seen once in 32 hours
    is the one worth keeping.  The common signatures are then stride-sampled so
    the mainstream cases stay represented rather than being cut wholesale.
    """
    groups: dict[tuple, list[Vector]] = {}
    for v in vecs:
        groups.setdefault(_signature(v), []).append(v)

    # deterministic representative per signature
    reps = {
        sig: min(members, key=lambda v: (v.msg_id, v.src_path, v.payload))
        for sig, members in groups.items()
    }
    order = sorted(groups, key=lambda s: (len(groups[s]), s))  # rarest first

    if len(order) > cap:
        keep_rare = int(cap * 0.75)
        rare, common = order[:keep_rare], order[keep_rare:]
        n = cap - keep_rare
        step = len(common) / n
        order = rare + [common[int(i * step)] for i in range(n)]

    picked = [reps[s] for s in order]
    return sorted(picked, key=lambda v: (v.tag, v.msg_id, v.exp_len, v.src_path, v.payload))


# ---------------------------------------------------------------- output

_HEADER = """\
# Generated by tools/logharvest.py -- do not hand-edit.
# Source: MeshCom firmware serial logs. Format: <name> <hex>, '#' comments.
"""


def hexs(s: str) -> str:
    return s.encode("ascii", "replace").hex().upper() or "-"


def write_corpora(h: Harvest, fuzz_dir: Path, vec_dir: Path, cap: int,
                  vec_cap: int) -> dict[str, int]:
    fuzz_dir.mkdir(parents=True, exist_ok=True)
    vec_dir.mkdir(parents=True, exist_ok=True)
    written: dict[str, int] = {}

    crc_sel = select_diverse(list(h.crc), cap)
    with (fuzz_dir / "crc_corpus.txt").open("w") as f:
        f.write(_HEADER)
        f.write(
            "# CRC-rejected frames, dumped by esp32_main.cpp:3977. Real on-air\n"
            "# corruption: the radio reported these as 255 bytes, so the tail past\n"
            "# the real frame is uninitialised RX-buffer content -- that is exactly\n"
            f"# what decodeAPRS() would have seen. {len(h.crc)} distinct dumps harvested,\n"
            f"# {len(crc_sel)} kept (bucketed by type byte, see select_diverse()).\n"
        )
        for i, hx in enumerate(crc_sel, 1):
            c = h.crc[hx]
            meta = f"  # rssi={c.rssi} snr={c.snr} ferr={c.freq_err}" if c.rssi is not None else ""
            f.write(f"c{i:04d} {hx}{meta}\n")
    written["crc_corpus.txt"] = len(crc_sel)

    if h.captured:
        cap_sel = select_diverse(list(h.captured), cap)
        with (fuzz_dir / "capture_corpus.txt").open("w") as f:
            f.write(_HEADER)
            f.write(
                "# Frames, die das Radio ANGENOMMEN hat -- byte-exakt aus dem\n"
                "# Mitschnitt (capture_functions.cpp, '--loradebug on' fuer RX,\n"
                "# '--txcapture on' fuer TX). Keine Rekonstruktion noetig.\n"
                f"# {len(h.captured)} distinct frames harvested, {len(cap_sel)} kept.\n"
            )
            for i, hx in enumerate(cap_sel, 1):
                d, rssi, snr = h.captured[hx]
                meta = f"  # {d}" + (f" rssi={rssi} snr={snr}" if rssi is not None else "")
                f.write(f"k{i:04d} {hx}{meta}\n")
        written["capture_corpus.txt"] = len(cap_sel)

    ack_sel = select_diverse(list(h.acks), cap)
    with (fuzz_dir / "ack_corpus.txt").open("w") as f:
        f.write(_HEADER)
        f.write(
            "# ACK frames rebuilt from printBuffer_ack() output, which prints every\n"
            "# byte of the 7- or 12-byte frame. Byte-exact, not a reconstruction.\n"
            f"# {len(h.acks)} distinct frames harvested, {len(ack_sel)} kept.\n"
        )
        for i, hx in enumerate(ack_sel, 1):
            f.write(f"a{i:04d} {hx}  # {h.acks[hx]}\n")
    written["ack_corpus.txt"] = len(ack_sel)

    vecs = select_vectors(list(h.vectors.values()), vec_cap)
    with (vec_dir / "reencode_vectors.txt").open("w") as f:
        f.write(
            "# Generated by tools/logharvest.py -- do not hand-edit.\n"
            "# Re-encode vectors from printBuffer_aprs() output.\n"
            "# Fields (whitespace separated, all hex unless noted):\n"
            "#   name tag type id hop srv trk mesh hw mod fcs fw lasthw fwsub explen\n"
            "#   srcpath-hex dstpath-hex payload-hex        ('-' = empty string)\n"
            "# tag=rx: printed straight out of decodeAPRS(), fcs/explen come from the\n"
            "#         wire and are a valid oracle for encodeAPRS().\n"
            "# tag=tx: printed from our own encoder's output -- circular, excluded\n"
            "#         from the oracle by the consumer.\n"
            f"# {len(h.vectors)} distinct vectors harvested, {len(vecs)} kept -- one per\n"
            f"# behavioural signature, see select_vectors(). "
            f"{sum(1 for v in vecs if v.tag == 'rx')} rx, "
            f"{sum(1 for v in vecs if v.tag == 'tx')} tx.\n"
        )
        for i, v in enumerate(vecs, 1):
            f.write(
                f"v{i:05d} {v.tag} {v.payload_type:02X} {v.msg_id:08X} {v.max_hop:02X} "
                f"{v.server} {v.track} {v.mesh:02X} {v.hw:02X} {v.mod:02X} {v.fcs:04X} "
                f"{v.fw:02X} {v.last_hw:02X} {v.fw_sub:02X} {v.exp_len:03d} "
                f"{hexs(v.src_path)} {hexs(v.dst_path)} {hexs(v.payload)}\n"
            )
    written["reencode_vectors.txt"] = len(vecs)
    return written


# ---------------------------------------------------------------- cli

def collect_files(inputs: list[str]) -> list[Path]:
    files: list[Path] = []
    for item in inputs:
        p = Path(item).expanduser()
        candidates = (
            sorted(q for q in p.rglob("*") if q.suffix in {".txt", ".log"})
            if p.is_dir()
            else [p]
        )
        for q in candidates:
            # the log directories also hold HTML analysis reports
            try:
                head = q.open("r", encoding="ascii", errors="replace").read(512)
            except OSError:
                continue
            if "<html" in head.lower() or head.lstrip().startswith("<"):
                continue
            files.append(q)
    return files


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("inputs", nargs="+", help="log files or directories")
    ap.add_argument("--fuzz-out", default="test/test_aprs_fuzz",
                    help="directory for crc_corpus.txt / ack_corpus.txt")
    ap.add_argument("--vec-out", default="test/test_aprs_reencode",
                    help="directory for reencode_vectors.txt")
    ap.add_argument("--cap", type=int, default=500, help="max frames per byte corpus (default 500)")
    ap.add_argument("--vec-cap", type=int, default=3000, help="max re-encode vectors (default 3000)")
    args = ap.parse_args()

    files = collect_files(args.inputs)
    if not files:
        print("no log files found", file=sys.stderr)
        return 1
    print(f"scanning {len(files)} file(s)")

    h = harvest(files)
    written = write_corpora(h, Path(args.fuzz_out), Path(args.vec_out),
                            args.cap, args.vec_cap)

    print(f"\n{h.stats['lines']:>9,} lines scanned")
    if h.stats["capture_seen"] or h.stats["capture_dropped"]:
        print(f"{h.stats['capture_seen']:>9,} MC-TEST capture frames "
              f"({h.stats['capture_truncated']:,} truncated, {len(h.captured):,} distinct, "
              f"{h.stats['capture_dropped']:,} dropped by the node)")
    print(f"{h.stats['crc_seen']:>9,} CRC_PAYLOAD dumps "
          f"({h.stats['crc_truncated']:,} truncated, {len(h.crc):,} distinct)")
    print(f"{h.stats['ack_seen']:>9,} ACK lines ({len(h.acks):,} distinct frames)")
    print(f"{h.stats['aprs_lines']:>9,} decoded frame lines "
          f"({h.stats['aprs_lines'] - h.stats['aprs_seen']:,} dropped: noise/unparsable)")
    print(f"{h.stats['aprs_seen']:>9,} usable "
          f"({h.stats['aprs_rx']:,} rx / {h.stats['aprs_tx']:,} tx, "
          f"{len(h.vectors):,} distinct vectors)")
    print()
    for name, n in written.items():
        where = args.vec_out if name.startswith("reencode") else args.fuzz_out
        print(f"  {where}/{name:<22} {n:>6,} records")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
