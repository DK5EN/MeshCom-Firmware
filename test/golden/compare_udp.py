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

So this compares two things separately:

  - the corpus records, in order, byte for byte -- these must be identical
  - the KEEP/BEAT records by count only, since their position is meaningless

A count difference is reported but does not fail on its own: how many
heartbeats fit inside a 37-datagram replay depends on when the replay started
relative to the node's timer. What would fail is a corpus record differing, a
record appearing or disappearing, or the order changing.

  python3 test/golden/compare_udp.py G0/<node>/udp G1/<node>/udp
  python3 test/golden/compare_udp.py --self-test

Exit 0 when the corpus records match, 1 otherwise.
"""
import argparse
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


def compare(a_dir: Path, b_dir: Path, prefix: str = "udp") -> List[str]:
    a_log, b_log = a_dir / f"{prefix}-log.txt", b_dir / f"{prefix}-log.txt"
    for p in (a_log, b_log):
        if not p.exists():
            return [f"missing capture: {p}"]

    a, b = read_log(a_log), read_log(b_log)
    a_corpus = [r for r in a if not is_volatile(r)]
    b_corpus = [r for r in b if not is_volatile(r)]
    a_vol = [r for r in a if is_volatile(r)]
    b_vol = [r for r in b if is_volatile(r)]

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

    print(f"{a_dir}: {len(a_corpus)} corpus + {len(a_vol)} KEEP/BEAT")
    print(f"{b_dir}: {len(b_corpus)} corpus + {len(b_vol)} KEEP/BEAT")
    if len(a_vol) != len(b_vol):
        print(f"note: KEEP/BEAT count differs ({len(a_vol)} vs {len(b_vol)}) -- "
              f"not a failure, the cadence is a wall-clock timer and depends on "
              f"when the replay started")
    if not problems:
        print(f"corpus records identical ({len(a_corpus)})")
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

    with tempfile.TemporaryDirectory() as d:
        def mk(name, text):
            p = Path(d) / name
            p.mkdir()
            (p / "udp-log.txt").write_text(text)
            return p
        a = mk("a", base)
        for name, text, want_ok in (("moved", moved, True),
                                    ("changed", changed, False),
                                    ("dropped", dropped, False)):
            b = mk(name, text)
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
