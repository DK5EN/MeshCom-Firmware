#!/usr/bin/env python3
"""CDC-02 port-open crash regression bench for native-USB ESP32-S3 boards.

Opening the USB serial port resets these boards (rst:0x15
USB_UART_CHIP_RESET, normal -- every open reboots the chip, that part is
expected). The bug is what can happen next: during boot the firmware resizes
the HWCDC TX ring buffer right after Serial.begin(), and if the host happens
to be polling the CDC endpoint at that instant, the ISR dereferences a NULL
ring buffer:

    assert failed: xRingbufferReceiveUpToFromISR ringbuf.c:1269

which is followed by 2-3 crash reboots (each usually logging
"esp_core_dump_flash: Not enough space" because the flash core-dump partition
fills up) and finally a boot that reports RESET_REASON=4 or a bare PANIC.
Observed on roughly 2 of a handful of port opens on the T-Deck Plus before
the fix. A secondary symptom sometimes travels with it: "SDCard: ERROR",
"Keyboard: ERROR" or "i2cRead returned Error" during the same boot, which
this script also tracks (but does not count as PASS/FAIL on its own).

This bench repeatedly opens and closes the port to hammer that boot-time
race: open, read boot output for --settle seconds (optionally hold the port
open --hold seconds longer), close, repeat immediately -- no pause between
iterations, since an immediate reopen is what triggered the bug in the
field.

Usage:
    python3 tools/bench/tdeck_cdc_portopen.py
    python3 tools/bench/tdeck_cdc_portopen.py --port /dev/cu.usbmodem2101 --iterations 100
    python3 tools/bench/tdeck_cdc_portopen.py --settle 6 --hold 1 --log tools/bench/runs/my_run.log

Exit code is 1 if any iteration showed an assert/crash signature, else 0.
"""
from __future__ import annotations

import argparse
import glob
import sys
import time
from pathlib import Path
from typing import List, Optional

import serial  # pyserial

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_LOG_DIR = REPO_ROOT / "tools" / "bench" / "runs"

CRASH_MARKERS = [
    "ringbuf.c",
    "assert failed",
    "Guru Meditation",
    "Not enough space to save core dump",
    "RESET_REASON=4",
    "PANIC",
]
SECONDARY_MARKERS = [
    "SDCard: ERROR",
    "Keyboard: ERROR",
    "i2cRead returned Error",
]


def autodetect_port() -> Optional[str]:
    """Return the first /dev/cu.usbmodem* device, or None if none is present."""
    matches = sorted(glob.glob("/dev/cu.usbmodem*"))
    return matches[0] if matches else None


def read_for(ser: serial.Serial, seconds: float) -> bytes:
    """Drain the port for `seconds`, tolerating stretches with no bytes at all
    (the S3 native USB drops the first ~256 B of boot output until the port
    is open, so a quiet iteration is not by itself a failure)."""
    buf = bytearray()
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        chunk = ser.read(4096)
        if chunk:
            buf += chunk
    return bytes(buf)


def hits(text: str, markers: List[str]) -> List[str]:
    return [m for m in markers if m in text]


def run_iteration(port: str, baud: float, settle: float, hold: float) -> bytes:
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = int(baud)
    ser.timeout = 0.2
    ser.dtr = False
    ser.rts = False
    ser.open()
    try:
        captured = bytearray(read_for(ser, settle))
        if hold > 0:
            captured += read_for(ser, hold)
    finally:
        ser.close()
    return bytes(captured)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default=None, help="serial port (default: first /dev/cu.usbmodem*)")
    ap.add_argument("--iterations", type=int, default=50, help="number of open/close cycles (default: 50)")
    ap.add_argument("--baud", type=float, default=115200, help="baud rate (default: 115200)")
    ap.add_argument("--settle", type=float, default=4.0, help="seconds to read boot output after each open (default: 4.0)")
    ap.add_argument("--hold", type=float, default=0.0, help="extra seconds to keep the port open before closing (default: 0.0, i.e. close immediately after settle)")
    ap.add_argument("--log", default=None, help="log file path (default: tools/bench/runs/cdc_portopen_<timestamp>.log)")
    args = ap.parse_args()

    port = args.port or autodetect_port()
    if not port:
        print("no /dev/cu.usbmodem* port found and --port not given", file=sys.stderr)
        return 2

    log_path = Path(args.log) if args.log else DEFAULT_LOG_DIR / f"cdc_portopen_{time.strftime('%Y%m%d-%H%M%S')}.log"
    log_path.parent.mkdir(parents=True, exist_ok=True)

    print(f"[cdc_portopen] port={port} baud={int(args.baud)} iterations={args.iterations} settle={args.settle}s hold={args.hold}s log={log_path}")

    crash_iterations: List[int] = []
    secondary_iterations: List[int] = []

    with open(log_path, "w", encoding="utf-8") as log:
        for i in range(1, args.iterations + 1):
            t0 = time.monotonic()
            try:
                data = run_iteration(port, args.baud, args.settle, args.hold)
            except serial.SerialException as exc:
                # A port that refuses to open at all (e.g. mid-reboot) is
                # itself worth recording, but is not the CDC-02 signature.
                data = f"[open failed: {exc}]".encode()
            elapsed = time.monotonic() - t0
            text = data.decode("utf-8", errors="replace")

            crash_hits = hits(text, CRASH_MARKERS)
            secondary_hits = hits(text, SECONDARY_MARKERS)
            if crash_hits:
                crash_iterations.append(i)
            if secondary_hits:
                secondary_iterations.append(i)

            log.write(f"\n===== iteration {i}/{args.iterations} ({elapsed:.2f}s, {len(data)} bytes) =====\n")
            if crash_hits:
                log.write(f"CRASH markers: {crash_hits}\n")
            if secondary_hits:
                log.write(f"secondary markers: {secondary_hits}\n")
            log.write(text)
            log.flush()

            status = "CRASH" if crash_hits else ("secondary" if secondary_hits else "ok")
            print(f"[{i:3d}/{args.iterations}] {len(data):5d} bytes  {status}")

    n_crash = len(crash_iterations)
    n_secondary = len(secondary_iterations)
    first_fail = crash_iterations[0] if crash_iterations else None

    print()
    print(f"[cdc_portopen] iterations={args.iterations} crash_iterations={n_crash} secondary_iterations={n_secondary} first_failing_iteration={first_fail}")
    print(f"[cdc_portopen] log: {log_path}")
    ok = n_crash == 0
    print(f"[cdc_portopen] {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
