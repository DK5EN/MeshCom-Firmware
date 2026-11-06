#!/usr/bin/env python3
"""Auto update heap gate right after web traffic (regression test for the T-Beam "Not enough free memory" refusal).

Each round requests the web pages, including the RX log, as a browser session does, then starts a check over USB
serial at once and reads the verdict. Exit 0 only if every round reached the server. Nothing is transmitted on LoRa
and nothing is installed.

    python3 tools/bench/au_gate_burst.py --port /dev/cu.usbserial-573C0005841 --host 192.168.68.71 --rounds 20

Opening the port reboots the node, which gives a clean start. Run identity_guard.py first; the raw log goes to
tools/bench/runs/harness/.
"""

from __future__ import annotations

import argparse
import re
import sys
import time
import urllib.request
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

PAGES = "messages wx position mheard path neighbours rxlog setup remote info messages mheard path setup info".split()
RE_OK = re.compile(r"\[AU\];check;tag;")
RE_FAIL = re.compile(r"\[AU\];fail;(\S+)")
RE_RESTART = re.compile(r"\[AU\];shed;restart")
RE_STARTED = re.compile(r"\[AU\];check;started")
RE_BOOT = re.compile(r"RESET_REASON=")
RE_ERR = re.compile(r"\[ERR\];update")


@dataclass
class Verdict:
    passed: int = 0
    restarted: int = 0
    failed: tuple[str, ...] = ()


def read_round(lines: list[str]) -> str:
    """Verdict of one round from the serial lines after its command: "pass", or why not."""
    started = False
    result = "no verdict"
    for line in lines:
        if RE_BOOT.search(line):
            return "reboot"
        if RE_ERR.search(line):
            return "not started"
        if RE_STARTED.search(line):
            started = True
        elif started and RE_OK.search(line):
            result = "pass"
        else:
            m = RE_FAIL.search(line)
            if started and m:
                result = m.group(1)
    return result if started else "not started"


def read_verdict(rounds: list[list[str]]) -> Verdict:
    """Counts the results of all rounds."""
    results = [read_round(r) for r in rounds]
    return Verdict(
        passed=results.count("pass"),
        restarted=sum(1 for r in rounds if any(RE_RESTART.search(line) for line in r)),
        failed=tuple(r for r in results if r != "pass"),
    )


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--port", required=True)
    ap.add_argument("--host", required=True)
    ap.add_argument("--rounds", type=int, default=20)
    ap.add_argument("--boot-s", type=float, default=70)
    ap.add_argument(
        "--job-s",
        type=float,
        default=26,
        help="wait after each check; the page requests take longer",
    )
    args = ap.parse_args()

    import serial  # only the bench run needs pyserial

    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    log = Path(__file__).parent / "runs" / "harness" / f"au_gate_burst_{stamp}.log"
    ser = serial.Serial()
    ser.port, ser.baudrate, ser.timeout, ser.dtr, ser.rts = args.port, 115200, 0.2, False, False
    ser.open()
    lines: list[str] = []
    buf = b""

    def pump(seconds: float) -> None:
        nonlocal buf
        end = time.monotonic() + seconds
        with log.open("a", encoding="utf-8") as f:
            while time.monotonic() < end:
                buf += ser.read(512)
                while b"\n" in buf:
                    raw, _, buf = buf.partition(b"\n")
                    text = raw.decode(errors="replace").rstrip()
                    lines.append(text)
                    f.write(text + "\n")

    pump(args.boot_s)
    rounds: list[list[str]] = []
    for _ in range(args.rounds):
        for page in PAGES:
            with urllib.request.urlopen(f"http://{args.host}/?page={page}", timeout=20) as r:
                r.read()
            pump(0.2)
        mark = len(lines)
        ser.write(b"--update check\n")
        pump(args.job_s)
        rounds.append(lines[mark:])

    v = read_verdict(rounds)
    print(
        f"rounds {args.rounds} passed {v.passed} restarted {v.restarted} failed {len(v.failed)} {sorted(set(v.failed))}"
    )
    print(f"log {log}")
    return 1 if v.failed else 0


if __name__ == "__main__":
    sys.exit(main())
