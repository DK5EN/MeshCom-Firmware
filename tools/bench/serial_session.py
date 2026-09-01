#!/usr/bin/env python3
"""Held-open serial session helper for the MeshCom bench.

Usage:
  serial_session.py PORT [--wait-boot] [--listen SECS] CMD [CMD ...]

Opens PORT once (dtr/rts False so ESP32 boards don't reset), optionally waits
for "CLIENT STARTED" (boot marker) before sending, sends each CMD followed by
CR, waits a beat between commands, then drains output for --listen seconds
(default 6) and prints everything received.
"""
import sys
import time

import serial  # pyserial

args = sys.argv[1:]
port = args.pop(0)
wait_boot = False
listen = 6.0
cmds = []
while args:
    a = args.pop(0)
    if a == "--wait-boot":
        wait_boot = True
    elif a == "--listen":
        listen = float(args.pop(0))
    else:
        cmds.append(a)

s = serial.Serial()
s.port = port
s.baudrate = 115200
s.timeout = 0.2
s.dtr = False
s.rts = False
s.open()

buf = b""


def drain(seconds):
    global buf
    end = time.time() + seconds
    while time.time() < end:
        chunk = s.read(4096)
        if chunk:
            buf += chunk


if wait_boot:
    end = time.time() + 25
    while time.time() < end and b"CLIENT STARTED" not in buf:
        chunk = s.read(4096)
        if chunk:
            buf += chunk
    time.sleep(1.5)

for c in cmds:
    s.write(c.encode() + b"\r")
    s.flush()
    drain(1.5)

drain(listen)
s.close()
sys.stdout.write(buf.decode("utf-8", errors="replace"))
