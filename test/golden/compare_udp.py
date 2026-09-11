#!/usr/bin/env python3
"""Compare two UDP-1990 captures (test plan step H6/H7).

`ble_golden.py --compare` exists because the BLE surface carries volatile
frames that will never repeat. The UDP surface has the same problem and had no
tool, so the first G1 comparison was done by hand -- which is not a gate.

**The volatile pair is KEEP and BEAT.** The node sends `KEEP` on a wall-clock
timer (`HEARTBEAT_INTERVAL`), not in response to anything in the corpus, and
the server answers each with a `BEAT`. Where that pair falls inside the replay
therefore shifts from run to run, and because the capture is a
length-prefixed concatenation in arrival order, one shifted pair makes the
whole `.bin` differ from that offset on. Measured on Heltec-93, G0 against G1:
751 differing bytes in `udp-rx.bin`, and the cause was the KEEP/BEAT pair
landing at record 26 in one run and 33 in the other. Every other record was
byte-identical.

There are two halves to a UDP capture and only the second one says anything
about the firmware:

  - `udp-log.txt`, the **server side**. Its `tx` records are what the stub
    sent, which is the corpus file -- comparing them mostly proves the stub
    replayed the same corpus. Its `rx` records are node-originated: `KEEP` on
    a timer, and `DATA` uploads of whatever the node happened to hear on the
    air during the capture. Those are live traffic and cannot be compared:
    T-Beam-92 uploaded two at G0 and one at G1.
  - `udp-rx-log.txt`, the **node side**: one `[GW];rx;type;<T>;len;<N>` per
    datagram the node accepted, which is what the node *made* of the corpus.
    This is the evidence step H6 exists for.

So both halves are compared with their volatile classes removed:

  server side   `tx` minus `BEAT`            must be identical
  node side     `[GW];rx` minus `BEAT`       must be identical
  everything else                            counted, not compared

Volatile, with the reason: `KEEP` and the node's `DATA` uploads are
node-originated and depend on live traffic; `BEAT` is the server answering a
`KEEP`, so it inherits that cadence; the NTP reply and the periodic
`[GW];srv;...` line are wall-clock driven.

A count difference in a volatile class is reported and does not fail. What
fails is a corpus datagram differing, appearing or disappearing, the order
changing, or the node classifying a datagram differently than it did before.

  python3 test/golden/compare_udp.py G0/<node>/udp G1/<node>/udp
  python3 test/golden/compare_udp.py --self-test

Exit 0 when the corpus records match, 1 otherwise.
"""
import argparse
import re
import sys
from pathlib import Path
from typing import List, Tuple

# Sent on a timer, not in response to the corpus: position is not comparable.
VOLATILE_INDICATORS = ("KEEP", "BEAT")

Record = Tuple[str, str, str, str]   # direction, indicator, length, hex payload


def read_log(path: Path) -> List[Record]:
    """Parse a recorder log line: `<t> <dir> <peer> ind=<I> len=<N> <hex>`.

    The timestamp and the peer are dropped: the clock is monotonic per run and
    the peer is the bench address of the day, neither of which is a property of
    the firmware.
    """
    out: List[Record] = []
    for line in path.read_text().splitlines():
        if not line.strip():
            continue
        f = line.split()
        if len(f) < 6:
            raise ValueError(f"{path}: cannot parse line: {line[:80]!r}")
        out.append((f[1], f[3], f[4], f[5]))
    return out


def is_volatile(rec: Record) -> bool:
    return any(v in rec[1] for v in VOLATILE_INDICATORS)


def corpus_records(recs: List[Record]) -> List[Record]:
    """Server side: what the stub sent, minus the heartbeat answers."""
    return [r for r in recs if r[0] == "tx" and "BEAT" not in r[1]]


NODE_RX = re.compile(r"\[GW\];rx;type;([A-Z]+);len;(\d+)")


def node_classifications(path: Path) -> List[Tuple[str, str]]:
    """Node side: what the node made of each datagram it accepted.

    BEAT is dropped for the same reason as on the server side -- it is the
    answer to a KEEP and follows the node's own timer, not the corpus.
    """
    out = []
    for m in NODE_RX.finditer(path.read_text()):
        if m.group(1) != "BEAT":
            out.append((m.group(1), m.group(2)))
    return out


def compare(a_dir: Path, b_dir: Path, prefix: str = "udp") -> List[str]:
    a_log, b_log = a_dir / f"{prefix}-log.txt", b_dir / f"{prefix}-log.txt"
    for p in (a_log, b_log):
        if not p.exists():
            return [f"missing capture: {p}"]

    a, b = read_log(a_log), read_log(b_log)
    a_corpus, b_corpus = corpus_records(a), corpus_records(b)
    problems: List[str] = []

    if len(a_corpus) != len(b_corpus):
        problems.append(
            f"corpus record count differs: {len(a_corpus)} vs {len(b_corpus)}")

    for i, (x, y) in enumerate(zip(a_corpus, b_corpus)):
        if x != y:
            problems.append(
                f"corpus record {i} differs:\n"
                f"    A {x[0]} ind={x[1]} {x[2]}\n"
                f"    B {y[0]} ind={y[1]} {y[2]}")
            if len(problems) >= 5:
                problems.append("    (further differences not listed)")
                break

    print(f"server side  A {len(a_corpus)} corpus datagrams, "
          f"B {len(b_corpus)}")
    if not problems:
        print(f"             identical")

    # the half that is about the firmware
    a_node = a_dir / f"{prefix}-rx-log.txt"
    b_node = b_dir / f"{prefix}-rx-log.txt"
    if not a_node.exists() or not b_node.exists():
        missing = a_node if not a_node.exists() else b_node
        problems.append(
            f"no node-side log at {missing} -- without it this comparison only "
            f"shows that the stub replayed the same corpus, not what the node "
            f"made of it, which is what step H6 is for")
        return problems

    an, bn = node_classifications(a_node), node_classifications(b_node)
    if len(an) != len(bn):
        problems.append(
            f"node accepted a different number of datagrams: "
            f"{len(an)} vs {len(bn)}")
    for i, (x, y) in enumerate(zip(an, bn)):
        if x != y:
            problems.append(
                f"node classification {i} differs: "
                f"A {x[0]} len={x[1]}  B {y[0]} len={y[1]}")
            if len(problems) >= 8:
                problems.append("    (further differences not listed)")
                break
    print(f"node side    A {len(an)} classified datagrams, B {len(bn)}")
    if not any("node" in p for p in problems):
        print(f"             identical")
    return problems


def self_test() -> int:
    import tempfile
    ok = True
    base = ("    0.100 tx 1.2.3.4:1990 ind=GATE len=91 aabb\n"
            "    1.100 tx 1.2.3.4:1990 ind=GATE len=93 ccdd\n"
            "    2.100 rx 1.2.3.4:1990 ind=KEEP len=46 eeff\n"
            "    2.200 tx 1.2.3.4:1990 ind=BEAT len=14 0011\n"
            "    3.100 tx 1.2.3.4:1990 ind=CONF len=79 2233\n")
    # same corpus, KEEP/BEAT moved to the end: must pass
    moved = ("    0.100 tx 1.2.3.4:1990 ind=GATE len=91 aabb\n"
             "    1.100 tx 1.2.3.4:1990 ind=GATE len=93 ccdd\n"
             "    3.100 tx 1.2.3.4:1990 ind=CONF len=79 2233\n"
             "    9.100 rx 1.2.3.4:1990 ind=KEEP len=46 eeff\n"
             "    9.200 tx 1.2.3.4:1990 ind=BEAT len=14 0011\n")
    # a corpus payload changed: must fail
    changed = base.replace("ind=CONF len=79 2233", "ind=CONF len=79 9999")
    # a corpus record vanished: must fail
    dropped = base.replace("    1.100 tx 1.2.3.4:1990 ind=GATE len=93 ccdd\n", "")

    node_ok = ("[GW];rx;type;DATA;len;91;ms;<NUM>\n"
               "[GW];rx;type;BEAT;len;14;ms;<NUM>\n"
               "[GW];rx;type;DATA;len;93;ms;<NUM>\n"
               "[GW];rx;type;CONF;len;79;ms;<NUM>\n")
    # same classifications, an extra BEAT: must pass
    node_beat = node_ok.replace("[GW];rx;type;CONF",
                                "[GW];rx;type;BEAT;len;19;ms;<NUM>\n[GW];rx;type;CONF")
    # the node classified a datagram differently: must fail
    node_bad = node_ok.replace("type;CONF;len;79", "type;DATA;len;79")

    with tempfile.TemporaryDirectory() as d:
        def mk(name, text, node=node_ok):
            p = Path(d) / name
            p.mkdir()
            (p / "udp-log.txt").write_text(text)
            if node is not None:
                (p / "udp-rx-log.txt").write_text(node)
            return p
        a = mk("a", base)
        for name, text, want_ok, node in (
                ("moved", moved, True, node_ok),
                ("changed", changed, False, node_ok),
                ("dropped", dropped, False, node_ok),
                ("extra-beat", base, True, node_beat),
                ("node-reclassified", base, False, node_bad),
                ("no-node-log", base, False, None)):
            b = mk(name, text, node)
            problems = compare(a, b)
            good = (not problems) if want_ok else bool(problems)
            print(f"  {'ok ' if good else 'FAIL'} {name}: "
                  f"{len(problems)} problem(s), expected "
                  f"{'none' if want_ok else 'some'}")
            ok = ok and good
    return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("a", nargs="?", type=Path, help="reference capture dir")
    ap.add_argument("b", nargs="?", type=Path, help="new capture dir")
    ap.add_argument("--prefix", default="udp")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()

    if args.self_test:
        return self_test()
    if not args.a or not args.b:
        ap.error("need two capture directories, or --self-test")

    problems = compare(args.a, args.b, args.prefix)
    for p in problems:
        print(f"DIFFERENCE {p}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
