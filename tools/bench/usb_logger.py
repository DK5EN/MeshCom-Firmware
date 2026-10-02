#!/usr/bin/env python3
"""Continuous USB-serial logger that stays attached across device resets (TD-17).

Native-USB ESP32-S3 boards (T-Deck, T-Deck Plus) reset when the port is opened
with DTR low (what serial_session.py and tdeck_harness.py do), so opening the
port *after* a crash destroys the panic output you came to read. Opening with
DTR high and RTS low attaches to the running node without a reset (verified on
DK5EN-14, 2026-09-11: uptime continuous across the attach, output flows at
once). This tool opens once that way, logs every line with a timestamp, and
re-opens only when the OS drops the device (USB re-enumeration after a chip
reset or a safeboot OTA), logging a marker so the gap is visible. Attach it
before the crash and leave it; it survives --reboot and a WiFi OTA cycle.

Port numbers follow the USB socket, not the board: confirm the node with
--info (callsign line) before trusting a port name.

Commands can be sent without touching the port from another process: append
lines to --cmdfile; they are sent character by character (the firmware
consumes one serial char per loop() and its USB RX buffer holds 256 bytes) and
the file is truncated afterwards.

Usage:
  usb_logger.py PORT --out LOG [--cmdfile CMD] [--baud 115200]

Log line format:  <ISO wall clock> <ms since attach>  <line>
Markers:          ### attached / ### lost (<error>) / ### sent: <cmd>
"""
import argparse
import datetime as dt
import os
import sys
import time

import serial  # pyserial


def now_iso():
    return dt.datetime.now().strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3]


class Logger:
    def __init__(self, path):
        self.f = open(path, "a", buffering=1)
        self.t0 = time.monotonic()

    def line(self, text):
        ms = int((time.monotonic() - self.t0) * 1000)
        self.f.write(f"{now_iso()} {ms:8d}  {text}\n")

    def marker(self, text):
        self.line(f"### {text}")
        self.f.flush()


def open_port(port, baud):
    s = serial.Serial()
    s.port = port
    s.baudrate = baud
    s.timeout = 0.1
    s.dtr = True    # DTR low would reset a native-USB S3 on open
    s.rts = False
    s.open()
    return s


def send_cmd(s, cmd, log):
    """One char per loop() on the firmware side; 32-byte chunks with a gap
    keep long lines under the 256-byte USB RX buffer (tdeck bench notes)."""
    data = cmd.encode()
    for i in range(0, len(data), 32):
        for b in data[i:i + 32]:
            s.write(bytes([b]))
            s.flush()
            time.sleep(0.005)
        time.sleep(0.05)
    s.write(b"\r")
    s.flush()
    log.marker(f"sent: {cmd}")


def poll_cmdfile(path):
    if not path or not os.path.exists(path):
        return []
    try:
        with open(path, "r+") as f:
            lines = [l.strip() for l in f.read().splitlines() if l.strip()]
            f.seek(0)
            f.truncate()
    except OSError:
        return []
    return lines


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port")
    ap.add_argument("--out", required=True)
    ap.add_argument("--cmdfile", default=None)
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--reopen-delay", type=float, default=0.5)
    args = ap.parse_args()

    log = Logger(args.out)
    s = None
    buf = b""
    last_cmd_poll = 0.0
    while True:
        if s is None:
            try:
                s = open_port(args.port, args.baud)
                log.marker(f"attached {args.port}")
            except (serial.SerialException, OSError) as e:
                time.sleep(args.reopen_delay)
                continue
        try:
            chunk = s.read(4096)
        except (serial.SerialException, OSError) as e:
            log.marker(f"lost ({e.__class__.__name__}: {e})")
            try:
                s.close()
            except Exception:
                pass
            s = None
            buf = b""
            continue
        if chunk:
            buf += chunk
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                log.line(raw.rstrip(b"\r").decode("utf-8", errors="replace"))
        if args.cmdfile and time.monotonic() - last_cmd_poll > 0.2:
            last_cmd_poll = time.monotonic()
            for cmd in poll_cmdfile(args.cmdfile):
                try:
                    send_cmd(s, cmd, log)
                except (serial.SerialException, OSError) as e:
                    log.marker(f"send failed ({e})")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
