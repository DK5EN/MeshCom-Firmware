#!/usr/bin/env python3
"""Compare two EXTUDP captures (test plan step H8).

The third comparison tool, after `ble_golden.py --compare` and
`compare_udp.py`, and written for the same reason: the surface carries
volatile traffic that will never repeat, so a raw diff says "differs" on a
firmware that did not change, and a hand analysis is not a gate.

**What is comparable: node-originated `msg` datagrams.** Those are the node's
responses to the corpus -- one per corpus entry it accepted -- and they are
deterministic in destination, body and order.

**What is not, with the reason each was found:**

| Class                          | Why it cannot be compared                                |
| ------------------------------ | -------------------------------------------------------- |
| `"src_type":"lora"`            | frames the node heard on the air and forwarded; live      |
|                                | traffic. Measured G0 -> G1: heltec-93 8 -> 3, rak-90 3 -> 0 |
| node `"type":"pos"` / `"tele"` | periodic beacons on their own timers. Whether one fires   |
|                                | inside the capture window is chance: heltec-93 had both   |
|                                | at G0 and neither at G1                                   |
| the `{NNN` suffix on a body    | the node's outgoing message counter, like `msg_id`        |

With those named, all four bench nodes compared clean between G0 and G1: eight
corpus messages each, identical destinations, bodies and order.

**Two limits of the G0 captures this tool cannot repair** (`GLD-01`):

- they are truncated at 160 characters, because they were made by redirecting
  `extudp_peer.py`'s print loop, so nothing past that point is in the
  baseline at all. Position frames are ~258 B.
- the normalizer masks the source address, so a G0 capture cannot be checked
  for cross-contamination -- the peer binds one port and every node with
  `--extudp on` pointing at the host delivers into it.

Both are why this compares `msg` bodies and destinations rather than whole
payloads: those fields sit inside the first 160 characters.

  python3 test/golden/compare_extudp.py G0/<node>/extudp G1/<node>/extudp
  python3 test/golden/compare_extudp.py --self-test

Exit 0 when the corpus responses match, 1 otherwise.
"""
import argparse
import re
import sys
from pathlib import Path
from typing import List, Tuple

Msg = Tuple[str, str]   # destination, body with the counter masked

COUNTER = re.compile(r"\{\d+$")


def corpus_messages(path: Path) -> List[Msg]:
    """The node's own `msg` datagrams, in order."""
    out: List[Msg] = []
    for line in path.read_text().splitlines():
        if '"src_type":"node"' not in line or '"type":"msg"' not in line:
            continue
        dst = re.search(r'"dst":"([^"]*)"', line)
        body = re.search(r'"msg":"([^"]*)"', line)
        out.append((dst.group(1) if dst else "?",
                    COUNTER.sub("{<CNT>", body.group(1)) if body else ""))
    return out


def volatile_counts(path: Path) -> Tuple[int, int]:
    text = path.read_text()
    lora = text.count('"src_type":"lora"')
    beacons = sum(1 for l in text.splitlines()
                  if '"src_type":"node"' in l
                  and ('"type":"pos"' in l or '"type":"tele"' in l))
    return lora, beacons


def compare(a_dir: Path, b_dir: Path) -> List[str]:
    a_f, b_f = a_dir / "extudp-received.txt", b_dir / "extudp-received.txt"
    for p in (a_f, b_f):
        if not p.exists():
            return [f"missing capture: {p}"]

    a, b = corpus_messages(a_f), corpus_messages(b_f)
    problems: List[str] = []

    if len(a) != len(b):
        problems.append(
            f"corpus response count differs: {len(a)} vs {len(b)}")
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            problems.append(
                f"corpus response {i} differs:\n"
                f"    A dst={x[0]!r} msg={x[1]!r}\n"
                f"    B dst={y[0]!r} msg={y[1]!r}")
            if len(problems) >= 6:
                problems.append("    (further differences not listed)")
                break

    al, ab = volatile_counts(a_f)
    bl, bb = volatile_counts(b_f)
    print(f"A {len(a)} corpus responses, {al} relayed, {ab} beacons")
    print(f"B {len(b)} corpus responses, {bl} relayed, {bb} beacons")
    if (al, ab) != (bl, bb):
        print("note: relayed/beacon counts differ -- not a failure, both are "
              "live traffic and timers")
    if not problems:
        print(f"corpus responses identical ({len(a)})")

    # the guard that had to exist after the broadcast incident of 2026-09-11:
    # check what the node actually emitted, never the corpus text
    for name, msgs in (("A", a), ("B", b)):
        star = [m for m in msgs if m[0] == "*"]
        if star:
            problems.append(
                f"BROADCAST in {name}: {len(star)} response(s) addressed to "
                f"'*' -- bench traffic goes to group 9/9999 or a direct "
                f"contact, never '*'")
    return problems


def self_test() -> int:
    import tempfile
    ok = True
    node = lambda t, dst, msg: (
        f'<ADDR>  150 {{"src_type":"node","type":"{t}",'
        f'"src":"DK5EN-92","dst":"{dst}","msg":"{msg}"}}')
    lora = ('<ADDR>  152 {"src_type":"lora","type":"msg",'
            '"src":"DK5EN-92,DK5EN-98","dst":"9999","msg":"an alle"}')
    beacon = ('<ADDR>  258 {"src_type":"node","type":"pos",'
              '"src":"DK5EN-92","msg":""}')

    base = "\n".join([node("msg", "DK5EN-1", "Test 1 2 3{674"),
                      node("msg", "9999", "an alle"), beacon, lora]) + "\n"
    # same responses, different live traffic and no beacon: must pass
    quiet = "\n".join([node("msg", "DK5EN-1", "Test 1 2 3{881"),
                       node("msg", "9999", "an alle")]) + "\n"
    # a response body changed: must fail
    changed = base.replace('"an alle"', '"etwas anderes"')
    # a response missing: must fail
    missing = "\n".join([node("msg", "DK5EN-1", "Test 1 2 3{674"), lora]) + "\n"
    # a broadcast: must fail
    broadcast = base.replace('"dst":"9999"', '"dst":"*"')

    with tempfile.TemporaryDirectory() as d:
        def mk(name, text):
            p = Path(d) / name
            p.mkdir()
            (p / "extudp-received.txt").write_text(text)
            return p
        a = mk("a", base)
        for name, text, want_ok in (("quiet-run", quiet, True),
                                    ("changed-body", changed, False),
                                    ("missing-response", missing, False),
                                    ("broadcast", broadcast, False)):
            problems = compare(a, mk(name, text))
            good = (not problems) if want_ok else bool(problems)
            print(f"  {'ok ' if good else 'FAIL'} {name}: {len(problems)} "
                  f"problem(s), expected {'none' if want_ok else 'some'}")
            ok = ok and good
    return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("a", nargs="?", type=Path)
    ap.add_argument("b", nargs="?", type=Path)
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()

    if args.self_test:
        return self_test()
    if not args.a or not args.b:
        ap.error("need two capture directories, or --self-test")

    problems = compare(args.a, args.b)
    for p in problems:
        print(f"DIFFERENCE {p}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
