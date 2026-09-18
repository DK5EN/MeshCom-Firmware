#!/usr/bin/env python3
"""Toggle-and-soak harness: flip one setting, watch for a reboot, repeat.

Holds the serial port open for the whole run (a CP2102 board reboots on every
port open, so opening once is the only way to see what a toggle does), sends
each step's command character by character, then listens for the soak period
and classifies what the node did:

  * a reboot that the step declared as by-design (`!` prefix) -> EXPECTED
  * any other reset banner, a panic, an abort, a watchdog -> CRASH
  * nothing of the kind -> OK

Steps (one per --step, in order):
  "--webserver off"          toggle, no reboot expected
  "!--gateway on"            toggle, a deferred by-design reboot is expected
  "wait"                     just listen for the soak period
  "run:<shell command>"      run a subprocess (e.g. a BLE cycler) while listening
  "probe:--info"             send a command, listen only --probe-seconds

Usage:
  toggle_soak.py PORT --out LOG --report REPORT [--soak 120] [--dtr auto|on|off]
                 [--wait-boot] --step CMD [--step CMD ...]

Exit code: 0 no crash, 1 at least one CRASH step, 2 harness error.
"""
import argparse
import datetime as dt
import re
import subprocess
import sys
import threading
import time

import serial  # pyserial

RESET_RE = re.compile(r"RESET_REASON=|RESETREAS=|rst:0x|CLIENT SETUP|\[BOOT\] RESET")
CRASH_RE = re.compile(
    r"Guru Meditation|abort\(\) was called|Backtrace:|Task watchdog|TWDT|"
    r"assert failed|Interrupt wdt|Stack canary|StackOverflow|Cache disabled|"
    r"LoadProhibited|StoreProhibited|IllegalInstruction|Rebooting\.\.\.|"
    r"panic'ed|CORRUPT HEAP|E \(\d+\) task_wdt"
)
BOOT_DONE_RE = re.compile(r"\[BOOT\][; ]ready|CLIENT STARTED")


def stamp() -> str:
    return dt.datetime.now().isoformat(timespec="milliseconds")


class Session:
    def __init__(self, port: str, dtr: bool, log_path: str):
        self.s = serial.Serial()
        self.s.port = port
        self.s.baudrate = 115200
        self.s.timeout = 0.2
        self.s.dtr = dtr
        self.s.rts = False
        self.s.open()
        self.log = open(log_path, "a", buffering=1)
        self.lines: list[str] = []
        self.lock = threading.Lock()
        self.stop = threading.Event()
        self.t = threading.Thread(target=self._rx, daemon=True)
        self.t.start()

    def _rx(self) -> None:
        pending = b""
        while not self.stop.is_set():
            try:
                d = self.s.read(4096)
            except serial.SerialException as e:
                self.mark(f"### serial lost ({e})")
                time.sleep(1)
                try:
                    self.s.close()
                    self.s.open()
                    self.mark("### serial reopened")
                except serial.SerialException:
                    pass
                continue
            if not d:
                continue
            pending += d
            while b"\n" in pending:
                line, pending = pending.split(b"\n", 1)
                text = line.decode("utf-8", "replace").rstrip("\r")
                with self.lock:
                    self.lines.append(text)
                self.log.write(f"{stamp()}  {text}\n")

    def mark(self, text: str) -> None:
        with self.lock:
            self.lines.append(text)
        self.log.write(f"{stamp()}  {text}\n")

    def send(self, cmd: str) -> None:
        self.mark(f"### sent: {cmd}")
        for ch in cmd + "\n":
            self.s.write(ch.encode())
            time.sleep(0.02)

    def cut(self) -> int:
        with self.lock:
            return len(self.lines)

    def since(self, idx: int) -> list[str]:
        with self.lock:
            return self.lines[idx:]

    def wait_for(self, regex: re.Pattern, start: int, seconds: float) -> bool:
        end = time.time() + seconds
        while time.time() < end:
            if any(regex.search(l) for l in self.since(start)):
                return True
            time.sleep(0.3)
        return False


def classify(lines: list[str]) -> tuple[bool, list[str]]:
    resets = [l for l in lines if RESET_RE.search(l)]
    crashes = [l for l in lines if CRASH_RE.search(l)]
    return bool(resets), resets + crashes


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("--out", required=True)
    ap.add_argument("--report", required=True)
    ap.add_argument("--soak", type=float, default=120)
    ap.add_argument("--probe-seconds", type=float, default=8)
    ap.add_argument("--dtr", default="auto", choices=["auto", "on", "off"])
    ap.add_argument("--wait-boot", action="store_true")
    ap.add_argument("--step", action="append", default=[])
    a = ap.parse_args()
    if not a.step:
        ap.error("at least one --step")
    dtr = {"on": True, "off": False}.get(a.dtr, "usbmodem" in a.port)

    ses = Session(a.port, dtr, a.out)
    ses.mark(f"### attached {a.port} dtr={dtr}")
    if a.wait_boot:
        ok = ses.wait_for(BOOT_DONE_RE, 0, 60)
        ses.mark(f"### boot marker {'seen' if ok else 'MISSING'}")
        time.sleep(3)

    rows = []
    worst = 0
    for step in a.step:
        expect_reboot = step.startswith("!")
        body = step[1:] if expect_reboot else step
        start = ses.cut()
        t0 = time.time()
        if body == "wait":
            ses.mark("### step wait")
            time.sleep(a.soak)
        elif body.startswith("run:"):
            ses.mark(f"### step run: {body[4:]}")
            p = subprocess.run(body[4:], shell=True, capture_output=True, text=True)
            ses.mark(f"### run rc={p.returncode}")
            for l in (p.stdout + p.stderr).splitlines():
                ses.mark(f"### run| {l}")
        elif body.startswith("probe:"):
            ses.send(body[6:])
            time.sleep(a.probe_seconds)
        else:
            ses.send(body)
            time.sleep(a.soak)
        lines = ses.since(start)
        rebooted, evidence = classify(lines)
        crashed = any(CRASH_RE.search(l) for l in lines)
        if crashed or (rebooted and not expect_reboot):
            verdict = "CRASH"
            worst = 1
        elif rebooted:
            verdict = "EXPECTED-REBOOT"
            # give the node its boot back before the next step
            ses.wait_for(BOOT_DONE_RE, start, 60)
            time.sleep(3)
        else:
            verdict = "OK"
        reason = next((l for l in lines if "RESET_REASON" in l or "RESETREAS" in l), "")
        rows.append((body, expect_reboot, verdict, len(lines), round(time.time() - t0, 1), reason, evidence[:6]))
        ses.mark(f"### verdict {verdict} for {body}")

    ses.stop.set()
    with open(a.report, "w") as r:
        r.write(f"# toggle soak {a.port} soak={a.soak}s {stamp()}\n\n")
        r.write("| step | reboot expected | verdict | lines | s | reset reason |\n|---|---|---|---|---|---|\n")
        for body, exp, verdict, n, secs, reason, _ in rows:
            r.write(f"| `{body}` | {'yes' if exp else 'no'} | {verdict} | {n} | {secs} | {reason.strip()} |\n")
        r.write("\n## evidence\n\n")
        for body, _, verdict, _, _, _, ev in rows:
            if ev:
                r.write(f"### {body} -> {verdict}\n")
                for l in ev:
                    r.write(f"    {l}\n")
        r.write(f"\n=== overall {'CRASH' if worst else 'PASS'}\n")
    print(open(a.report).read())
    return worst


if __name__ == "__main__":
    try:
        sys.exit(main())
    except serial.SerialException as e:
        print(f"harness error: {e}", file=sys.stderr)
        sys.exit(2)
