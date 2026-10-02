#!/usr/bin/env python3
"""Held-open serial session helper for the MeshCom bench.

Usage:
  serial_session.py PORT [options] [CMD ...]

Options:
  --wait-boot           wait for a boot marker before sending commands
  --boot-timeout SECS   how long to wait for that marker (default 30)
  --boot-marker {any,ready}
                        which marker --wait-boot accepts (default any).
                        "ready" waits for "[BOOT];ready" only -- use it on an
                        ESP32, where "CLIENT STARTED" fires before the command
                        parser is usable and swallows the first command.
  --listen SECS         drain window after the last command (default 6)
  --eol {lf,cr,crlf}    command terminator (default lf)
  --baud N              default 115200
  --dtr {auto,on,off}   DTR on open (default auto: on for native-USB ports,
                        off for USB-UART bridges -- see below)
  --strict              exit 2 if --wait-boot saw no marker, or if nothing
                        was received at all
  -h, --help            this text

Opens PORT once, optionally waits for the node to finish booting, sends each
CMD, then drains output for --listen seconds. Everything received is streamed
to stdout as it arrives; the run summary goes to stderr, so redirecting stdout
to a file still gives a clean log.

Boot markers
------------
Waits for "[BOOT];ready", which both platforms emit with a raw Serial.printf
(src/esp32/esp32_main.cpp, src/nrf52/nrf52_main.cpp). "CLIENT STARTED" is also
accepted, but do NOT rely on it: on the ESP32 that line goes through
printlndeb() and is suppressed unless debug output is on, so waiting for it
against a normal ESP32 image times out. Only the nRF52 prints it
unconditionally.

The ESP32 marker is itself conditional: it fires once the network is up, or
immediately if no network is wanted. A node configured for WiFi that never
gets an IP never prints it. So a missing marker is a warning, not a fatal
error -- the session continues and says so. Use --strict to make it fatal.

CP2102 boards (Heltec, T-Beam) reset when the port is opened, so --wait-boot
sees a real boot. Native-USB boards (RAK4631, ESP32-S3) do NOT reset on open:
against an already-running RAK there is no boot to wait for and --wait-boot
will simply burn --boot-timeout before continuing. Leave it off there unless
you are power-cycling the node yourself.

DTR
---
A native-USB board (RAK4631, ESP32-S3) implements CDC in firmware and only
sends once the host asserts DTR: open a RAK with DTR low and it stays mute --
zero bytes, no error. A USB-UART bridge (CP2102, CH340) is the opposite case:
there DTR/RTS drive EN/BOOT on the ESP32, so asserting them resets or
re-flashes the board.

--dtr auto (the default) picks by port name: "usbmodem" means native USB, so
DTR is asserted; anything else is treated as a bridge and DTR stays low.
Override with --dtr on / --dtr off when the guess is wrong.

Terminator
----------
Commands are LF-terminated by default. The firmware's command parser strips a
trailing 0x0a (see --setcall in src/command_functions.cpp); CR-only input is
not recognised, which silently swallows the command -- the node keeps printing
its normal log and the run looks like it worked. If a command appears to have
no effect, check the "received" counter in the summary and confirm the state
changed (e.g. with --info) before believing it.
"""

import os
import sys
import time

try:
    import serial  # pyserial
except ImportError:
    sys.stderr.write("serial_session: pyserial not installed (pip install pyserial)\n")
    raise SystemExit(1)

BOOT_MARKERS = (b"[BOOT];ready", b"CLIENT STARTED")
# "CLIENT STARTED" fires long before the command parser is usable on an ESP32
# that still has to bring up WiFi: a command sent on that marker is swallowed
# and the run looks like it worked. --boot-marker ready waits for the real
# end-of-boot line instead.
BOOT_MARKERS_READY = (b"[BOOT];ready",)
EOL = {"lf": b"\n", "cr": b"\r", "crlf": b"\r\n"}


def die(msg):
    sys.stderr.write("serial_session: %s\n" % msg)
    raise SystemExit(1)


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        sys.stdout.write(__doc__)
        return 0

    port = argv.pop(0)
    if port.startswith("-"):
        die("first argument must be the port, got %r (see --help)" % port)

    wait_boot = False
    strict = False
    listen = 6.0
    boot_timeout = 30.0
    baud = 115200
    dtr = None  # None => auto
    eol = EOL["lf"]
    boot_markers = BOOT_MARKERS
    cmds = []

    def need(flag, rest):
        if not rest:
            die("%s needs a value" % flag)
        return rest.pop(0)

    while argv:
        a = argv.pop(0)
        if a == "--wait-boot":
            wait_boot = True
        elif a == "--strict":
            strict = True
        elif a == "--listen":
            listen = float(need(a, argv))
        elif a == "--boot-timeout":
            boot_timeout = float(need(a, argv))
        elif a == "--baud":
            baud = int(need(a, argv))
        elif a == "--dtr":
            v = need(a, argv)
            if v not in ("auto", "on", "off"):
                die("--dtr must be auto, on or off")
            dtr = None if v == "auto" else (v == "on")
        elif a == "--boot-marker":
            v = need(a, argv)
            if v not in ("any", "ready"):
                die("--boot-marker must be any or ready")
            boot_markers = BOOT_MARKERS if v == "any" else BOOT_MARKERS_READY
        elif a == "--eol":
            v = need(a, argv)
            if v not in EOL:
                die("--eol must be one of %s" % ", ".join(EOL))
            eol = EOL[v]
        elif a in ("-h", "--help"):
            sys.stdout.write(__doc__)
            return 0
        else:
            cmds.append(a)

    if dtr is None:
        # Native-USB CDC (RAK4631, ESP32-S3) stays mute until the host asserts
        # DTR; a USB-UART bridge uses those lines for EN/BOOT and must not see
        # them asserted. macOS/Linux both name native-USB ports *usbmodem*.
        dtr = "usbmodem" in port

    s = serial.Serial()
    s.port = port
    s.baudrate = baud
    s.timeout = 0.2
    s.dtr = dtr
    s.rts = False
    try:
        s.open()
    except serial.SerialException as e:
        die("could not open %s: %s" % (port, e))

    received = 0
    marker_seen = None

    def pump(seconds, until_marker=False):
        """Read for `seconds`, streaming to stdout. Returns early on a marker."""
        nonlocal received, marker_seen
        tail = b""
        end = time.time() + seconds
        while time.time() < end:
            chunk = s.read(4096)
            if not chunk:
                continue
            received += len(chunk)
            try:
                sys.stdout.write(chunk.decode("utf-8", errors="replace"))
                sys.stdout.flush()
            except BrokenPipeError:
                # downstream closed (head, less q). Stop writing, keep the port
                # close in the finally block, and exit like a normal pipeline.
                os.dup2(os.open(os.devnull, os.O_WRONLY), sys.stdout.fileno())
                raise SystemExit(0)
            if until_marker:
                # keep a small overlap so a marker split across reads is found
                tail = (tail + chunk)[-256:]
                for m in boot_markers:
                    if m in tail:
                        marker_seen = m.decode()
                        return True
        return False

    try:
        if wait_boot:
            if not pump(boot_timeout, until_marker=True):
                sys.stderr.write(
                    "serial_session: WARNING no boot marker (%s) within %.0fs -- "
                    "continuing anyway. A native-USB board (RAK4631, S3) does not "
                    "reset when the port is opened, so there may be no boot to "
                    "wait for; an ESP32 that never gets an IP also never prints "
                    "it.\n" % (" or ".join(m.decode() for m in boot_markers), boot_timeout)
                )
            else:
                time.sleep(1.5)  # let the post-marker burst settle

        for c in cmds:
            s.write(c.encode() + eol)
            s.flush()
            pump(1.5)

        pump(listen)
    finally:
        s.close()

    sys.stderr.write(
        "serial_session: port=%s baud=%d dtr=%s eol=%r commands=%d received=%dB boot_marker=%s\n"
        % (port, baud, "on" if dtr else "off",
           eol.decode("unicode_escape"), len(cmds), received, marker_seen or "none")
    )
    if received == 0:
        sys.stderr.write(
            "serial_session: WARNING nothing was received -- the node may be "
            "silent, held by another process, or on a different port.\n"
        )

    if strict and ((wait_boot and marker_seen is None) or received == 0):
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
