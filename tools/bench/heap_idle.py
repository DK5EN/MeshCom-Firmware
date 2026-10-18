#!/usr/bin/env python3
"""Release-gate idle-heap figure of a node.

Captures the ESP32 firmware lines
    [HEAP];<time>;<free>;<minfree>;<maxalloc>;(init|mon|<service>)
    [PSRM];<time>;<free>[;(<label>)]
from a node, waits --idle-s after the first line and reports init free,
boot-mark labels, last (mon) free, minimum free and largest block.

The ';' separators only reach the console with --debug csv. Otherwise printfdeb()
(src/printfdeb_format.h) turns every ';' into a space, so the usual line is
    [HEAP] 00:00:03 182340 180000 110580 (init)
and the parser accepts both forms. ESP32 only: nRF52 prints a different form
(`<time>;[HEAP];<free>;<min>;<maxalloc>;(init|mon)` via Serial.printf), which is
not parsed here.

With --setlog on the node prints no [HEAP] (mon) lines; the heap then appears once
per 300 s window in the STAT line
    HH:MM:SS [LOG] STAT util=9 ... heap=70484 ... fw=40a/20261016 up=300 ...
which is parsed as a sample labelled "stat" (free = heap=, no min/maxalloc; fw= is
used as the firmware version). The idle figure is the last (mon) or STAT heap,
whichever came later. The default --idle-s 600 therefore yields two STAT samples.

Channel A (default): USB serial (--port or --usb-serial <serial number>).
  Opening the port resets an ESP32 -- wanted, the boot is captured.
Channel B: --host <ip>, net console 2323 with HMAC login (password from env
  MC_CONSOLE_PW; protocol as in tools/hmac_connect.py).

Baseline: --baseline tools/heap_baseline.json --board ttgo_tbeam
  [--tolerance 3072] compares the idle free; exit 1 if lower by more than the
  tolerance. --update-baseline stores the figure instead.

Raw capture goes to tools/bench/runs/harness/ (raw *.log is gitignored).
Requires: Python 3.9+, pyserial for channel A only.
"""

from __future__ import annotations

import argparse
import hashlib
import hmac
import json
import os
import re
import socket
import sys
import time
from dataclasses import dataclass, field
from datetime import date
from pathlib import Path
from typing import Iterator

HARNESS_DIR = Path(__file__).resolve().parent / "runs" / "harness"

# Separator is ';' (--debug csv) or ' ' (printfdeb default). The time field may
# itself contain a space, so anchor on the trailing numbers and the (label).
_SEP = r"[; ]+"
_HEAP_RE = re.compile(
    r"\[HEAP\]" + _SEP + r"(?P<time>.*?)" + _SEP
    + r"(?P<free>\d+)" + _SEP + r"(?P<minfree>-?\d+)" + _SEP + r"(?P<maxalloc>-?\d+)"
    + _SEP + r"\((?P<label>[^()]*)\)"
)
_PSRM_RE = re.compile(
    r"\[PSRM\]" + _SEP + r"(?P<time>.*?)" + _SEP + r"(?P<free>\d+)"
    + r"(?:" + _SEP + r"\((?P<label>[^()]*)\))?\s*$"
)
_STAT_RE = re.compile(r"\[LOG\]\s+STAT\b(?P<rest>.*)")
_STAT_HEAP_RE = re.compile(r"\bheap=(?P<heap>\d+)")
_STAT_FW_RE = re.compile(r"\bfw=(?P<fw>\S+)")
_VERSION_RE = re.compile(
    r"(?:version|firmware|meshcom)\D{0,12}?(\d\.\d{2}[a-z]?(?:\.\d+)*)", re.IGNORECASE
)


class HeapError(Exception):
    """Capture or parse failure with a message meant for the operator."""


@dataclass
class HeapSample:
    time: str
    free: int
    minfree: int | None
    maxalloc: int | None
    label: str


def is_sample_line(line: str) -> bool:
    """True for a line that carries a heap sample: [HEAP] or a STAT line with heap=."""
    if "[HEAP]" in line:
        return True
    m = _STAT_RE.search(line)
    return bool(m and _STAT_HEAP_RE.search(m["rest"]))


@dataclass
class HeapReport:
    samples: list[HeapSample] = field(default_factory=list)
    psram: list[tuple[str, int, str]] = field(default_factory=list)
    version: str = "unknown"
    _fw_seen: bool = False

    def feed(self, line: str) -> bool:
        """Parse one console line; return True if it was a heap sample ([HEAP] or STAT)."""
        st = _STAT_RE.search(line)
        if st:
            fw = _STAT_FW_RE.search(st["rest"])
            if fw and not self._fw_seen:
                self.version = fw["fw"]
                self._fw_seen = True
            hp = _STAT_HEAP_RE.search(st["rest"])
            if hp:
                self.samples.append(
                    HeapSample(line[: st.start()].strip(), int(hp["heap"]), None, None, "stat")
                )
                return True
            return False
        if self.version == "unknown":
            m = _VERSION_RE.search(line)
            if m and "[HEAP]" not in line:
                self.version = m.group(1)
        m = _HEAP_RE.search(line)
        if m:
            self.samples.append(
                HeapSample(
                    m["time"], int(m["free"]), int(m["minfree"]),
                    int(m["maxalloc"]), m["label"].strip(),
                )
            )
            return True
        m = _PSRM_RE.search(line)
        if m:
            self.psram.append((m["time"], int(m["free"]), (m["label"] or "").strip()))
        return False

    def figures(self) -> dict:
        if not self.samples:
            raise HeapError(
                "no [HEAP] line received: wrong channel, DO_DEBUG off or node did not boot"
            )
        init = next((s for s in self.samples if s.label == "init"), None)
        marks = [
            {"label": s.label, "free": s.free}
            for s in self.samples
            if s.label not in ("init", "mon")
        ]
        mons = [s for s in self.samples if s.label == "mon"]
        stats = [s for s in self.samples if s.label == "stat"]
        periodic = [s for s in self.samples if s.label in ("mon", "stat")]
        last = periodic[-1] if periodic else self.samples[-1]
        minfrees = [s.minfree for s in self.samples if s.minfree is not None]
        allocs = [s.maxalloc for s in self.samples if s.maxalloc is not None]
        return {
            "init_free": init.free if init else None,
            "boot_marks": marks,
            "last_mon_free": mons[-1].free if mons else None,
            "last_stat_free": stats[-1].free if stats else None,
            "idle_free": last.free,
            "min_free": min(minfrees) if minfrees else None,
            "min_free_seen": min(s.free for s in self.samples),
            "largest_block": max(allocs) if allocs else None,
            "psram_last_free": self.psram[-1][1] if self.psram else None,
            "samples": len(self.samples),
            "firmware_version": self.version,
        }


def compare_baseline(idle_free: int, baseline_free: int, tolerance: int) -> tuple[bool, int]:
    """Return (ok, delta); delta = idle - baseline. Fails if delta < -tolerance."""
    delta = idle_free - baseline_free
    return delta >= -tolerance, delta


def load_baseline(path: Path) -> dict:
    if not path.exists():
        return {}
    return json.loads(path.read_text(encoding="utf-8"))


def update_baseline(path: Path, board: str, figures: dict, channel: str) -> None:
    data = load_baseline(path)
    data[board] = {
        "idle_free": figures["idle_free"],
        "firmware_version": figures["firmware_version"],
        "date": date.today().isoformat(),
        "channel": channel,
    }
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")


# -- channels ---------------------------------------------------------------


def resolve_usb_serial(serial_number: str) -> str:
    from serial.tools import list_ports  # type: ignore[import-not-found]

    for p in list_ports.comports():
        if p.serial_number == serial_number:
            return p.device
    raise HeapError(f"no USB serial port with serial number {serial_number!r}")


def serial_lines(
    port: str, deadline_s: float, stop_after_first_s: float, boot_timeout_s: float | None = None
) -> Iterator[str]:
    import serial  # type: ignore[import-not-found]

    with serial.Serial(port, 115200, timeout=0.5) as ser:
        yield from _read_lines(ser.read, deadline_s, stop_after_first_s, boot_timeout_s)


def hmac_login(sock: socket.socket, password: str) -> None:
    """Challenge/response as tools/hmac_connect.py: NONCE -> HMAC-SHA256 -> OK."""
    buf = b""
    while b"\n" not in buf:
        chunk = sock.recv(256)
        if not chunk:
            raise HeapError("connection closed before NONCE challenge")
        buf += chunk
    line, _, rest = buf.partition(b"\n")
    text = line.decode(errors="replace").strip()
    if not text.upper().startswith("NONCE:"):
        raise HeapError(f"unexpected console greeting: {text!r}")
    nonce = bytes.fromhex(text.split()[1])
    sock.sendall((hmac.new(password.encode(), nonce, hashlib.sha256).hexdigest() + "\n").encode())
    buf = rest
    while b"\n" not in buf:
        chunk = sock.recv(256)
        if not chunk:
            raise HeapError("connection closed during login")
        buf += chunk
    result = buf.split(b"\n", 1)[0].decode(errors="replace").strip()
    if result != "OK":
        raise HeapError(f"console login failed: {result!r}")


def net_lines(
    host: str, deadline_s: float, stop_after_first_s: float, boot_timeout_s: float | None = None
) -> Iterator[str]:
    password = os.environ.get("MC_CONSOLE_PW", "")
    sock = socket.create_connection((host, 2323), timeout=10)
    try:
        if password:
            hmac_login(sock, password)
        sock.settimeout(0.5)

        def read(n: int) -> bytes:
            try:
                return sock.recv(n)
            except socket.timeout:
                return b""

        yield from _read_lines(read, deadline_s, stop_after_first_s, boot_timeout_s)
    finally:
        sock.close()


def _read_lines(
    read, deadline_s: float, idle_s: float, boot_timeout_s: float | None = None
) -> Iterator[str]:
    """Yield lines until deadline_s (hard cap) or idle_s after the first [HEAP] line.

    Raises HeapError if no [HEAP] line arrived within boot_timeout_s.
    """
    start = time.monotonic()
    first: float | None = None
    buf = b""
    while True:
        now = time.monotonic()
        if now - start > deadline_s:
            return
        if first is None and boot_timeout_s is not None and now - start > boot_timeout_s:
            raise HeapError(f"no [HEAP] line within --boot-timeout-s {boot_timeout_s:g}")
        if first is not None and now - first >= idle_s:
            return
        data = read(256)
        if not data:
            continue
        buf += data
        while b"\n" in buf:
            raw, _, buf = buf.partition(b"\n")
            line = raw.decode(errors="replace").rstrip("\r")
            if first is None and is_sample_line(line):
                first = time.monotonic()
            yield line


def run_capture(lines: Iterator[str], log_path: Path | None) -> HeapReport:
    report = HeapReport()
    log = log_path.open("w", encoding="utf-8") if log_path else None
    try:
        for line in lines:
            if log:
                log.write(line + "\n")
            report.feed(line)
    finally:
        if log:
            log.close()
    return report


# -- main -------------------------------------------------------------------


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ch = ap.add_mutually_exclusive_group(required=True)
    ch.add_argument("--port", help="serial device (channel A)")
    ch.add_argument("--usb-serial", help="USB serial number, resolved via pyserial (channel A)")
    ch.add_argument("--host", help="node IP, net console 2323 (channel B)")
    ap.add_argument("--idle-s", type=float, default=600, help="seconds to wait after the first [HEAP] line")
    ap.add_argument("--boot-timeout-s", type=float, default=60, help="max wait for the first [HEAP] line")
    ap.add_argument("--out", type=Path, help="write JSON figures here")
    ap.add_argument("--baseline", type=Path)
    ap.add_argument("--board")
    ap.add_argument("--tolerance", type=int, default=3072)
    ap.add_argument("--update-baseline", action="store_true")
    args = ap.parse_args(argv)

    if (args.baseline or args.update_baseline) and not (args.baseline and args.board):
        ap.error("--baseline and --board are required for compare/update")

    try:
        deadline = args.boot_timeout_s + args.idle_s + 30
        if args.host:
            channel, lines = "net2323", net_lines(args.host, deadline, args.idle_s, args.boot_timeout_s)
        else:
            port = args.port or resolve_usb_serial(args.usb_serial)
            channel, lines = "usb", serial_lines(port, deadline, args.idle_s, args.boot_timeout_s)
        HARNESS_DIR.mkdir(parents=True, exist_ok=True)
        log_path = HARNESS_DIR / f"heap_idle_{time.strftime('%Y%m%d_%H%M%S')}.log"
        report = run_capture(lines, log_path)
        figures = report.figures()
    except (HeapError, OSError) as exc:
        print(f"heap_idle: error: {exc}", file=sys.stderr)
        return 2
    figures["channel"] = channel
    figures["raw_log"] = str(log_path)

    rc = 0
    if args.baseline:
        if args.update_baseline:
            update_baseline(args.baseline, args.board, figures, channel)
            figures["baseline"] = "updated"
        else:
            entry = load_baseline(args.baseline).get(args.board)
            if entry is None:
                print(f"heap_idle: error: no baseline for board {args.board!r}", file=sys.stderr)
                return 2
            ok, delta = compare_baseline(figures["idle_free"], entry["idle_free"], args.tolerance)
            figures["baseline"] = {
                "idle_free": entry["idle_free"], "delta": delta,
                "tolerance": args.tolerance, "result": "pass" if ok else "fail",
            }
            rc = 0 if ok else 1

    text = json.dumps(figures, indent=2)
    print(text)
    if args.out:
        args.out.write_text(text + "\n", encoding="utf-8")
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
