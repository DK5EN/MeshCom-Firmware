#!/usr/bin/env python3
"""NTP-01 bench regression: drive `--ntpsync` N times, reduce the async
[NTP];... markers NtpAsync already emits (ntp_async.cpp) to a per-attempt
outcome and an rtt distribution.

`--ntpsync` (command_functions.cpp, INSTRUMENT_ENABLED bench block, modelled
on the `--srvip` hook) only ever *triggers* NtpAsync::requestNow() -- the
class is non-blocking by design (src/ntp_async.h), so the outcome of each
attempt arrives asynchronously on the regular [NTP];... line, not as a reply
to the command itself. This script waits for that line after each request.

Usage
  cd tools/bench/runs
  python3 ../experiments/ntpsync.py --port /dev/cu.usbserial-0001
  python3 ../experiments/ntpsync.py --port /dev/cu.usbserial-0001 --loops 20 --interval 5
  python3 ../experiments/ntpsync.py --parse-only ntpsync_20260831-*/session.log

Markers consumed (all raw Serial.printf, so they survive --debug off):
  [NTPSYNC];requested                              command ack, this session
  [NTPSYNC];busy;...                                a request was already in flight
  [NTPSYNC];err;...                                 e.g. no IP address yet
  [NTP];ok;epoch;E;rtt;R                            ntp_async.cpp tryConsume()
  [NTP];timeout;ip;IP;fails;F                       ntp_async.cpp loop()
  [NTP];txfail;ip;IP;fails;F                        ntp_async.cpp loop()
  [NTP];kod;ip;IP                                   ntp_async.cpp tryConsume() (stratum 0)
"""

import argparse
import glob
import os
import re
import statistics
import sys
import time

try:
    import serial  # pyserial
except ImportError:  # pragma: no cover - exercised only when running for real
    serial = None

RE_REQUESTED = re.compile(r"\[NTPSYNC\];requested")
RE_BUSY = re.compile(r"\[NTPSYNC\];busy")
RE_ERR = re.compile(r"\[NTPSYNC\];err;(?P<why>.*)")
RE_OK = re.compile(r"\[NTP\];ok;epoch;(?P<epoch>\d+);rtt;(?P<rtt>\d+)")
RE_TIMEOUT = re.compile(r"\[NTP\];timeout;ip;(?P<ip>[\d.]+);fails;(?P<fails>\d+)")
RE_TXFAIL = re.compile(r"\[NTP\];txfail;ip;(?P<ip>[\d.]+);fails;(?P<fails>\d+)")
RE_KOD = re.compile(r"\[NTP\];kod;ip;(?P<ip>[\d.]+)")

# an outstanding request times out at 2500 ms (NTP_ASYNC_TIMEOUT_MS,
# ntp_async.cpp) -- give the round trip plus scheduling slack.
ATTEMPT_TIMEOUT_S = 6.0


def pct(vals, q):
    if not vals:
        return None
    v = sorted(vals)
    k = min(len(v) - 1, int(round(q * (len(v) - 1))))
    return v[k]


def classify(line):
    """Pure line -> outcome-dict classifier, no I/O -- exercised directly by
    --parse-only and easy to unit-test without a serial port."""
    m = RE_OK.search(line)
    if m:
        return {"outcome": "ok", "rtt": int(m.group("rtt")), "epoch": int(m.group("epoch"))}
    m = RE_TIMEOUT.search(line)
    if m:
        return {"outcome": "timeout", "fails": int(m.group("fails"))}
    m = RE_TXFAIL.search(line)
    if m:
        return {"outcome": "txfail", "fails": int(m.group("fails"))}
    if RE_KOD.search(line):
        return {"outcome": "kod"}
    m = RE_ERR.search(line)
    if m:
        return {"outcome": "err", "why": m.group("why").strip()}
    if RE_BUSY.search(line):
        return {"outcome": "busy"}
    return None


class Reducer:
    """Turns one session's line stream into per-attempt outcomes. Pure --
    only ever sees lines, never a serial port (see run() below)."""

    def __init__(self):
        self.attempts = []  # list of outcome dicts, one per --ntpsync

    def add(self, line):
        c = classify(line)
        if c is not None:
            self.attempts.append(c)
        return c

    def summary(self):
        n = len(self.attempts)
        by = {}
        for a in self.attempts:
            by[a["outcome"]] = by.get(a["outcome"], 0) + 1
        rtts = [a["rtt"] for a in self.attempts if a["outcome"] == "ok"]

        out = [f"ntpsync attempts               {n}"]
        for k in sorted(by, key=lambda x: -by[x]):
            out.append(f"    {k:<10} {by[k]}")
        if rtts:
            out.append(
                f"rtt ms (ok only)              n={len(rtts)} "
                f"min={min(rtts)} median={int(statistics.median(rtts))} "
                f"p90={pct(rtts, 0.9)} max={max(rtts)}"
            )
        if n:
            out.append(f"success rate                  {by.get('ok', 0)}/{n}")
        return "\n".join(out)


# --------------------------------------------------------------------------
# Hardware runner
# --------------------------------------------------------------------------

BOOT_TIMEOUT = 40.0
RE_READY = re.compile(r"\[BOOT\][; ]ready")


def open_port(port, baud=115200):
    s = serial.Serial()
    s.port = port
    s.baudrate = baud
    s.timeout = 0.2
    s.dtr = False
    s.rts = False
    s.open()
    return s


def send(ser, cmd):
    # the firmware consumes one char per loop(): pace the characters (same
    # convention as srvprobe.py/wifisoak.py).
    for ch in cmd:
        ser.write(ch.encode())
        ser.flush()
        time.sleep(0.02)
    ser.write(b"\n")
    ser.flush()


class LineSource:
    def __init__(self, ser, logf):
        self.ser = ser
        self.logf = logf
        self.buf = b""

    def pump(self, timeout):
        end = time.time() + timeout
        lines = []
        while time.time() < end:
            chunk = self.ser.read(4096)
            if not chunk:
                continue
            self.buf += chunk
            while b"\n" in self.buf:
                raw, self.buf = self.buf.split(b"\n", 1)
                line = raw.decode("utf-8", errors="replace").rstrip("\r")
                wall = time.strftime("%Y-%m-%d %H:%M:%S")
                self.logf.write(f"{wall} {line}\n")
                self.logf.flush()
                lines.append(line)
        return lines

    def wait_for(self, pattern, timeout):
        end = time.time() + timeout
        while time.time() < end:
            for line in self.pump(min(1.0, max(0.05, end - time.time()))):
                if pattern.search(line):
                    return line
        return None


def run(port, loops, interval, outdir):
    if serial is None:
        sys.exit("pyserial is required to run against hardware (or use --parse-only)")

    os.makedirs(outdir, exist_ok=True)
    logpath = os.path.join(outdir, "session.log")
    red = Reducer()

    with open(logpath, "w", encoding="utf-8") as logf:
        ser = open_port(port)
        print(f"opened {port} (this resets the node) -- waiting for [BOOT];ready")
        src = LineSource(ser, logf)
        if src.wait_for(RE_READY, BOOT_TIMEOUT) is None:
            print("WARNING: no [BOOT];ready seen after opening the port -- continuing anyway")

        for i in range(1, loops + 1):
            send(ser, "--ntpsync")
            outcome = None
            end = time.time() + ATTEMPT_TIMEOUT_S
            while time.time() < end and outcome is None:
                for line in src.pump(min(0.5, max(0.05, end - time.time()))):
                    # classify() never matches the [NTPSYNC];requested ack --
                    # only a real [NTP];... outcome, or a same-session
                    # [NTPSYNC];busy|err refusal, ends the wait.
                    c = red.add(line)
                    if c is not None:
                        outcome = c
                        break
            if outcome is None:
                print(f"{i}/{loops}: no outcome within {ATTEMPT_TIMEOUT_S:.0f}s")
                red.attempts.append({"outcome": "silent"})
            else:
                print(f"{i}/{loops}: {outcome}")
            if i < loops:
                time.sleep(interval)

        ser.close()

    print()
    print(red.summary())
    with open(os.path.join(outdir, "summary.txt"), "w", encoding="utf-8") as fh:
        fh.write(f"ntpsync {outdir}\n\n{red.summary()}\n")
    print(f"\n{outdir}/summary.txt")
    return red


def parse_only(paths):
    red = Reducer()
    for p in paths:
        with open(p, encoding="utf-8", errors="replace") as fh:
            for line in fh:
                red.add(line[20:] if len(line) > 20 else line)
    print(red.summary())


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default="/dev/cu.usbserial-0001")
    ap.add_argument("--loops", type=int, default=10, help="number of --ntpsync requests")
    ap.add_argument("--interval", type=float, default=3.0, help="seconds between requests")
    ap.add_argument("--label", default="")
    ap.add_argument("--parse-only", nargs="+", metavar="LOG", help="re-reduce saved session.log file(s)")
    args = ap.parse_args()

    if args.parse_only is not None:
        paths = []
        for pat in args.parse_only:
            paths.extend(sorted(glob.glob(pat)))
        if not paths:
            sys.exit("no logs matched")
        parse_only(paths)
        return

    outdir = f"ntpsync_{args.label + '_' if args.label else ''}{time.strftime('%Y%m%d-%H%M%S')}"
    run(args.port, args.loops, args.interval, outdir)


if __name__ == "__main__":
    main()
