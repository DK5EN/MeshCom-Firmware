#!/usr/bin/env python3
"""Scheduled test DMs between two own nodes for a soak run.

    python3 tools/bench/soak_dm.py --hours 24 --interval-min 30 \
        --node DK5EN-1=192.168.68.62 --node DK5EN-98=dk5en-98.local \
        --log ~/meshlog/soak-dm-20260927.log

Every interval each node sends one DM to the other, the two directions
offset by half an interval so they never share a slot. A DM goes out through
the node's own web server (GET /?sendmessage&tocall=..&message=..), exactly
as the web GUI sends it; the firmware appends the "{NNN" message number.

Only direct DMs between the listed nodes -- never "*" or a group
(operator rule: no broadcast test messages). The text carries a run tag and
a sequence number ("soak 20260927 #7 DK5EN-1>DK5EN-98") so tools/soakstatus.py
can match each DM with its ack in the node logs.

One line per send is appended to --log:
    <iso time>  SEND <seq> <from> <to> <http status> <node answer>
Stdlib only. Stop with Ctrl-C / SIGTERM.
"""

from __future__ import annotations

import argparse
import datetime as dt
import signal
import sys
import time
import urllib.parse
import urllib.request
from pathlib import Path

_stop = False


def _on_signal(signum: int, frame: object) -> None:
    global _stop
    _stop = True


def parse_node(text: str) -> tuple[str, str]:
    call, sep, host = text.partition("=")
    if not sep or not call or not host:
        raise argparse.ArgumentTypeError(f"expected CALL=HOST, got {text!r}")
    return call.upper(), host


def send_dm(host: str, to_call: str, text: str, timeout: float = 15.0) -> tuple[int, str]:
    query = "sendmessage&tocall=" + urllib.parse.quote(to_call) + "&message=" + urllib.parse.quote(text)
    url = f"http://{host}/?{query}"
    try:
        req = urllib.request.Request(url, headers={"X-MC": "1"})  # CSRF guard of the node web server
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            body = resp.read(200).decode("utf-8", "replace")
            answer = "ok" if "sendmessage ok" in body else ("refused" if "refused" in body else "other")
            return resp.status, answer
    except Exception as exc:  # network errors are logged, the run continues
        return 0, f"error:{type(exc).__name__}"


def log_line(path: Path, line: str) -> None:
    with path.open("a", encoding="utf-8") as fh:
        fh.write(line + "\n")
    print(line, flush=True)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--node", action="append", type=parse_node, required=True,
                    help="CALL=HOST, exactly two")
    ap.add_argument("--hours", type=float, default=24.0)
    ap.add_argument("--interval-min", type=float, default=30.0,
                    help="per direction; the two directions are offset by half of it")
    ap.add_argument("--tag", default=dt.date.today().strftime("%Y%m%d"))
    ap.add_argument("--log", type=Path, required=True)
    args = ap.parse_args()

    if len(args.node) != 2:
        ap.error("exactly two --node CALL=HOST are needed")
    (call_a, host_a), (call_b, host_b) = args.node
    if call_a == call_b:
        ap.error("the two nodes must differ")

    signal.signal(signal.SIGTERM, _on_signal)
    signal.signal(signal.SIGINT, _on_signal)

    args.log.parent.mkdir(parents=True, exist_ok=True)
    half = args.interval_min * 60.0 / 2.0
    end = time.time() + args.hours * 3600.0
    # alternate: A->B, (half interval), B->A, (half interval), A->B ...
    plan = [(call_a, host_a, call_b), (call_b, host_b, call_a)]
    seq = 0
    log_line(args.log, f"{dt.datetime.now().isoformat(timespec='seconds')}  START tag={args.tag} "
                       f"{call_a}={host_a} {call_b}={host_b} interval_min={args.interval_min} hours={args.hours}")
    next_at = time.time()
    while not _stop and time.time() < end:
        now = time.time()
        if now < next_at:
            time.sleep(min(5.0, next_at - now))
            continue
        frm, host, to = plan[seq % 2]
        seq += 1
        text = f"soak {args.tag} #{seq} {frm}>{to}"
        status, answer = send_dm(host, to, text)
        log_line(args.log, f"{dt.datetime.now().isoformat(timespec='seconds')}  SEND {seq} {frm} {to} {status} {answer}")
        next_at += half
    log_line(args.log, f"{dt.datetime.now().isoformat(timespec='seconds')}  END sent={seq}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
