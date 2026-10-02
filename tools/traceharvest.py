#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
"""Harvest decision traces from MeshCom firmware logs for offline replay.

Where ``logharvest.py`` extracts *bytes* (frame corpora), this extracts
*decisions*: the ordered sequence of choices a running node made about
deduplication, transmit-queue priority and ACK handling.  Replaying that
sequence against the firmware modules compiled natively answers a question no
bench test can: **does today's code still decide the way the field decided?**

The traces are not a model of the firmware — they are the firmware's own
`[MC-DBG]` output.  The replay drives the real `is_new_packet()`,
`addLoraRxBuffer()` and `getMessagePriority()`, so a disagreement means the
code and the field have parted ways, not that a reimplementation drifted.

Three traces are produced:

``dedup_trace.txt``
    Every dedup decision in order (NEW / DUP+slot / ADD+slot).  Replaying it
    reproduces ring occupancy exactly, so slot allocation and eviction are
    fenced against ~5000 real decisions per node.

``txprio_trace.txt``
    Every ``RING_WRITE``+``RING_PRIO`` pair, joined with the frame's decoded
    fields so the ring slot can be rebuilt byte-exact via ``encodeAPRS()``.
    Fences the priority classifier — including the destination-parsing branch
    that decides broadcast vs. group vs. personal.

``ack_honoured.txt``
    ACK frames the node demonstrably acted on (an ``ACK_RECEIVED`` closed a
    pending ring slot).  ``isPlausibleAckFrame()`` postdates these logs, so
    there is no logged verdict to compare against — instead the replay asserts
    the filter would not have rejected traffic the field honoured.

Usage::

    uv run tools/traceharvest.py ~/Downloads/dg0opk ~/Downloads/dj8meh
"""

from __future__ import annotations

import argparse
import re
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from logharvest import (  # noqa: E402
    collect_files, hexs, parse_ack, parse_aprs, strip_prefix,
)

# ---------------------------------------------------------------- trace events

_DEDUP_NEW = re.compile(r"\[MC-DBG\] RX_DEDUP_NEW msg_id=([0-9A-F]{8})")
_DEDUP_DUP = re.compile(r"\[MC-DBG\] RX_DEDUP_DUP msg_id=([0-9A-F]{8}) slot=(\d+)")
_DEDUP_ADD = re.compile(
    r"\[MC-DBG\] RX_DEDUP_ADD msg_id=([0-9A-F]{8}) srv=(\d) slot=(\d+)/(\d+)")

_RING_WRITE = re.compile(
    r"\[MC-DBG\] RING_WRITE slot=(\d+) type=([0-9A-F]{2}) status=([0-9A-F]{2}) "
    r"len=(\d+) msg_id=([0-9A-F]{8})")
_RING_PRIO = re.compile(r"\[MC-DBG\] RING_PRIO slot=(\d+) prio=(\d+)")

_ACK_RECEIVED = re.compile(r"\[MC-DBG\] ACK_RECEIVED retid=(\d+) msg_id=([0-9A-F]{8})")


def harvest(paths: list[Path]) -> tuple[list, list, list, Counter]:
    stats = Counter()
    dedup: list[tuple] = []
    txprio: list[tuple] = []
    acked_ids: set[str] = set()
    ack_frames: dict[str, str] = {}      # acked-msg-id -> ACK frame hex
    fields: dict[str, object] = {}       # msg_id -> Vector from the decoded line

    last_slot: dict[str, int] = {}

    for path in paths:
        # Each file is one node; a dedup ring is per node, so the traces are
        # emitted per file and the replay resets between them.
        node = path.stem
        stats["files"] += 1
        pending_write: dict[str, tuple] = {}

        with path.open("r", encoding="ascii", errors="replace") as fh:
            for raw in fh:
                text = strip_prefix(raw)

                if (m := _DEDUP_NEW.search(text)) is not None:
                    dedup.append((node, "N", m.group(1), 0, 0))
                    stats["dedup_new"] += 1
                    continue
                if (m := _DEDUP_DUP.search(text)) is not None:
                    dedup.append((node, "D", m.group(1), int(m.group(2)), 0))
                    stats["dedup_dup"] += 1
                    continue
                if (m := _DEDUP_ADD.search(text)) is not None:
                    slot, ring = int(m.group(3)), int(m.group(4))
                    # Der Schreibzeiger laeuft strikt +1 modulo Ringgroesse.
                    # Springt er, hat der Knoten neu gestartet (oder das Log
                    # begann mitten im Betrieb). Das als Marke fuehren, statt
                    # sich auf einen Banner-String zu verlassen: die Luecke in
                    # der Slotfolge IST der Nachweis, unabhaengig davon, was
                    # die Firmware beim Hochlauf ausgibt.
                    prev = last_slot.get(node)
                    if prev is None or slot != (prev + 1) % ring:
                        dedup.append((node, "R", "00000000", slot, 0))
                        stats["dedup_resync"] += 1
                    last_slot[node] = slot
                    dedup.append((node, "A", m.group(1), slot, int(m.group(2))))
                    stats["dedup_add"] += 1
                    continue

                if (m := _RING_WRITE.search(text)) is not None:
                    # RING_PRIO follows on the next line and carries the verdict
                    pending_write[m.group(1)] = (
                        m.group(5), int(m.group(2), 16), int(m.group(3), 16), int(m.group(4)))
                    stats["ring_write"] += 1
                    continue
                if (m := _RING_PRIO.search(text)) is not None:
                    w = pending_write.pop(m.group(1), None)
                    if w is not None:
                        txprio.append((node, w[0], w[1], w[2], w[3], int(m.group(2))))
                    continue

                if (m := _ACK_RECEIVED.search(text)) is not None:
                    acked_ids.add(m.group(2))
                    stats["ack_received"] += 1
                    continue

                if (a := parse_ack(text)) is not None:
                    hx = a[1]
                    if len(hx) == 24:      # 12-byte ACK carries the acked id
                        acked = bytes.fromhex(hx)[6:10][::-1].hex().upper()
                        ack_frames.setdefault(acked, hx)
                    continue

                if (v := parse_aprs(text)) is not None:
                    fields.setdefault(f"{v.msg_id:08X}", v)

    honoured = sorted((mid, ack_frames[mid]) for mid in acked_ids if mid in ack_frames)
    stats["ack_honoured"] = len(honoured)
    stats["ack_unmatched"] = len(acked_ids) - len(honoured)

    # getMessagePriority() reads the frame's destination only for USER text
    # (type 0x3A that is not a relay). Everything else is decided from the type
    # and status bytes alone, so those records replay without the join -- which
    # matters, because ACK ring writes never produce a printBuffer_aprs line and
    # would otherwise be missing from the trace entirely.
    joined = []
    for n, mid, t, st, ln, pr in txprio:
        v = fields.get(mid)
        needs_body = (t == 0x3A and st != 0xFF)
        if v is None and needs_body:
            stats["txprio_unreplayable"] += 1
            continue
        joined.append((n, mid, t, st, ln, pr, v))
    stats["txprio_total"] = len(txprio)
    stats["txprio_joined"] = len(joined)
    return dedup, joined, honoured, stats


# ---------------------------------------------------------------- output

def write_traces(dedup, txprio, honoured, out_dir: Path, cap: int) -> dict[str, int]:
    out_dir.mkdir(parents=True, exist_ok=True)
    written: dict[str, int] = {}

    # The dedup trace must stay CONTIGUOUS -- it is a sequence, not a set.
    # Capping means keeping a prefix per node, never sampling across it.
    per_node: dict[str, list] = {}
    for ev in dedup:
        per_node.setdefault(ev[0], []).append(ev)
    kept = []
    for node in sorted(per_node):
        kept.extend(per_node[node][:cap])

    with (out_dir / "dedup_trace.txt").open("w") as f:
        f.write(
            "# Generated by tools/traceharvest.py -- do not hand-edit.\n"
            "# Dedup-Entscheidungsfolge echter Knoten, in Reihenfolge.\n"
            "#   <node> N <msg_id>              is_new_packet() -> true\n"
            "#   <node> D <msg_id> <slot>       is_new_packet() -> false, Treffer in <slot>\n"
            "#   <node> A <msg_id> <slot> <srv> addLoraRxBuffer() schrieb in <slot>\n"
            "#   <node> R -        <slot>       Neustart: Schreibzeiger sprang,\n"
            "#                                  Ringinhalt ab hier unbekannt\n"
            "# Die Folge ist zusammenhaengend: ein Praefix je Knoten, nie eine\n"
            "# Stichprobe -- der Ringzustand ergibt sich nur aus der Historie.\n"
            f"# {len(dedup)} Ereignisse geerntet, {len(kept)} behalten "
            f"(max {cap} je Knoten).\n")
        for node, kind, mid, slot, srv in kept:
            if kind == "N":
                f.write(f"{node} N {mid}\n")
            elif kind == "D":
                f.write(f"{node} D {mid} {slot}\n")
            elif kind == "R":
                f.write(f"{node} R - {slot}\n")
            else:
                f.write(f"{node} A {mid} {slot} {srv}\n")
    written["dedup_trace.txt"] = len(kept)

    # Priority classification is order-independent -- each record stands alone,
    # so deduplicating on the decision-relevant fields is safe here.
    seen = set(); uniq = []
    for node, mid, typ, status, ln, prio, v in txprio:
        key = ((typ, status, prio, v.dst_path, len(v.src_path), v.payload[:1])
               if v is not None else (typ, status, prio, None, ln, None))
        if key in seen:
            continue
        seen.add(key); uniq.append((mid, typ, status, ln, prio, v))
    uniq = uniq[:cap]

    with (out_dir / "txprio_trace.txt").open("w") as f:
        f.write(
            "# Generated by tools/traceharvest.py -- do not hand-edit.\n"
            "# getMessagePriority()-Entscheidungen echter Knoten.\n"
            "# Felder: name type status len prio  src-hex dst-hex payload-hex\n"
            "#         (type/status hex, prio dezimal; '-' = leerer String)\n"
            "# Der Ringslot wird beim Replay byte-exakt neu aufgebaut, deshalb\n"
            "# stehen hier die Frame-Felder und nicht nur der Typ: die Prioritaet\n"
            "# einer Textnachricht haengt am ZIEL, das RING_WRITE nicht mitloggt.\n"
            f"# {len(txprio)} Entscheidungen geerntet, {len(uniq)} verschiedene behalten.\n")
        for i, (mid, typ, status, ln, prio, v) in enumerate(uniq, 1):
            body = (f"{hexs(v.src_path)} {hexs(v.dst_path)} {hexs(v.payload)}"
                    if v is not None else "- - -")
            f.write(f"t{i:05d} {typ:02X} {status:02X} {ln:03d} {prio} {body}\n")
    written["txprio_trace.txt"] = len(uniq)

    with (out_dir / "ack_honoured.txt").open("w") as f:
        f.write(
            "# Generated by tools/traceharvest.py -- do not hand-edit.\n"
            "# ACK-Frames, die ein Knoten nachweislich verarbeitet hat: auf sie\n"
            "# folgte ein ACK_RECEIVED, das einen wartenden Ringslot geschlossen hat.\n"
            "# isPlausibleAckFrame() ist juenger als diese Logs -- es gibt also kein\n"
            "# geloggtes Urteil zum Vergleich. Geprueft wird stattdessen, dass der\n"
            "# Filter keinen Frame verwirft, den das Feld honoriert hat.\n"
            f"# {len(honoured)} Frames.\n")
        for mid, hx in honoured:
            f.write(f"{hx}  # ackt x{mid}\n")
    written["ack_honoured.txt"] = len(honoured)
    return written


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("inputs", nargs="+", help="log files or directories")
    ap.add_argument("--out", default="test/support/traces", help="output directory")
    ap.add_argument("--cap", type=int, default=1200,
                    help="max dedup events per node / max txprio records (default 1200)")
    args = ap.parse_args()

    files = collect_files(args.inputs)
    if not files:
        print("no log files found", file=sys.stderr)
        return 1
    print(f"scanning {len(files)} file(s)")

    dedup, txprio, honoured, stats = harvest(files)
    written = write_traces(dedup, txprio, honoured, Path(args.out), args.cap)

    print(f"\n  dedup:  {stats['dedup_new']:>7,} NEW  {stats['dedup_dup']:>7,} DUP  "
          f"{stats['dedup_add']:>7,} ADD  {stats['dedup_resync']:>4,} Neustarts")
    print(f"  txprio: {stats['txprio_total']:>7,} Entscheidungen, "
          f"{stats['txprio_joined']:,} replaybar "
          f"({stats['txprio_unreplayable']:,} Nutzertexte ohne Frameinhalt verworfen)")
    print(f"  ack:    {stats['ack_received']:>7,} ACK_RECEIVED, "
          f"{stats['ack_honoured']:,} davon einem Frame zuzuordnen "
          f"({stats['ack_unmatched']:,} ohne Frame)")
    print()
    for name, n in written.items():
        print(f"  {args.out}/{name:<20} {n:>7,} Saetze")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
