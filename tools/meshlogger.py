#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
"""Long-run capture of a node's debug console (TCP 2323) to disk.

This is the source end of the log toolchain: ``logharvest.py`` and
``traceharvest.py`` turn the files it writes into test corpora and replay
traces, and ``loganalyse.sh`` reads them directly.  Getting a useful corpus
means leaving a node logging for a day or two, which a foreground ``nc`` or an
SSH session cannot do.

The console (``src/net_console.h``, ESP32 only) is a bidirectional serial
bridge, so this also switches the node into the verbose mode worth recording:

* ``--loradebug on``  -- the ``[MC-DBG]`` decision trace and the RX side of the
  raw-frame capture
* ``--txcapture on``  -- the TX side of the capture

Both settings live in flash and survive a node reboot, so they are sent once.
The state found on arrival is restored when the run ends.

**The console accepts exactly one client.** ``net_console.cpp`` closes the
existing socket when a new client authenticates, so connecting while this runs
kicks the logger, which reconnects five seconds later and kicks you back. To
take the console over without killing the run, create a ``PAUSE`` file in the
output directory; the logger drops the connection and waits until it is gone.

Output is ``<outdir>/YYYY-MM-DD.log``, one line per console line, prefixed with
a timestamp in the format ``serial_monitor.py`` uses -- both harvesters strip
it. Files are appended, so restarting the logger does not discard the day.
``<outdir>/status.txt`` is rewritten every minute with progress and free space.

Volume is roughly 200 B/s with both debug flags on, so about 36 MB for 48 h.

Usage::

    uv run tools/meshlogger.py dk5en-98.local --hours 48
    uv run tools/meshlogger.py dk5en-98.local --password geheim --hours 24

Unattended, on a machine that stays up (screen survives the SSH session, but
not a reboot -- use a systemd unit if the run has to outlive one)::

    screen -dmS meshlog python3 meshlogger.py dk5en-98.local --hours 48 \
        --outdir ~/meshlog/dk5en-98
    screen -r meshlog            # attach, Ctrl-A D to detach
    touch ~/meshlog/dk5en-98/PAUSE     # release the console
    rm ~/meshlog/dk5en-98/PAUSE        # hand it back

Only the standard library is used, so it runs on a Raspberry Pi Zero with the
system Python and no virtualenv.
"""

import argparse
import hashlib
import hmac
import os
import re
import shutil
import signal
import socket
import sys
import time
from datetime import datetime, timezone

CONNECT_TIMEOUT = 10.0
READ_TIMEOUT = 5.0
BACKOFF_MIN = 5
BACKOFF_MAX = 60
# TM-50: read-silence watchdog. With the debug flags on the node emits
# something at least every 10 s (CHANNEL_UTIL/ONRXDONE_STATS), without them
# still KEEP/BEAT and relay traffic -- 90 s of silence therefore means the
# TCP connection is a zombie (overnight soak 2026-09-01: the node's WLAN
# outage killed its side of the session without a FIN ever reaching us; the
# logger sat on recv() timeouts for 2.4 h without noticing). A false
# positive on a genuinely mute node costs one harmless reconnect.
STALL_TIMEOUT = 90.0

stop_requested = False


def on_signal(signum, frame):
    global stop_requested
    stop_requested = True


def stamp():
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]


class Console:
    """One connection to the node console, including the HMAC handshake."""

    def __init__(self, host, port, password):
        self.host = host
        self.port = port
        self.password = password.encode() if password else b""
        self.sock = None

    def connect(self):
        self.sock = socket.create_connection((self.host, self.port), timeout=CONNECT_TIMEOUT)
        self.sock.settimeout(CONNECT_TIMEOUT)

        # TM-50: belt and braces underneath the read-silence watchdog in
        # main(). Keepalive probes make the kernel notice a peer that
        # vanished without a FIN (WLAN outage) and turn later recv() calls
        # into a hard error instead of endless timeouts. The tunables differ
        # per platform (macOS: TCP_KEEPALIVE = idle; Linux: TCP_KEEPIDLE/
        # KEEPINTVL/KEEPCNT), so each is set only where it exists.
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
        for opt, val in (("TCP_KEEPIDLE", 30), ("TCP_KEEPINTVL", 10),
                         ("TCP_KEEPCNT", 3), ("TCP_KEEPALIVE", 30)):
            if hasattr(socket, opt):
                try:
                    self.sock.setsockopt(socket.IPPROTO_TCP, getattr(socket, opt), val)
                except OSError:
                    pass

        # The server sends either "NONCE: <hex>" (password set) or "OK"
        # followed by the banner (no password).
        head = b""
        while b"\n" not in head:
            chunk = self.sock.recv(256)
            if not chunk:
                raise ConnectionError("closed before the banner arrived")
            head += chunk

        line, _, rest = head.partition(b"\n")
        text = line.decode(errors="replace").strip()

        if text.startswith("NONCE:"):
            nonce = text.split(":", 1)[1].strip()
            digest = hmac.new(self.password, bytes.fromhex(nonce), hashlib.sha256).hexdigest()
            self.sock.sendall((digest + "\n").encode())
            resp = self.sock.recv(256).decode(errors="replace")
            if not resp.startswith("OK"):
                raise ConnectionError("authentication rejected: %r" % resp[:40])
            rest = b""

        self.sock.settimeout(READ_TIMEOUT)
        return rest

    def send(self, command):
        self.sock.sendall((command + "\n").encode())

    def close(self):
        if self.sock:
            try:
                self.sock.close()
            except OSError:
                pass
            self.sock = None


class Sink:
    """One log file per day, one timestamped line per console line."""

    def __init__(self, outdir):
        self.outdir = outdir
        self.day = None
        self.fh = None
        self.lines = 0
        self.bytes = 0
        os.makedirs(outdir, exist_ok=True)

    def _rotate(self):
        day = datetime.now().strftime("%Y-%m-%d")
        if day != self.day:
            if self.fh:
                self.fh.close()
            self.day = day
            path = os.path.join(self.outdir, day + ".log")
            # Append: restarting the logger must not discard the day.
            self.fh = open(path, "a", buffering=1, errors="replace")

    def write(self, text):
        self._rotate()
        line = "%s  %s\n" % (stamp(), text)
        self.fh.write(line)
        self.lines += 1
        self.bytes += len(line)

    def close(self):
        if self.fh:
            self.fh.close()
            self.fh = None


def free_mb(path):
    return shutil.disk_usage(path).free // (1024 * 1024)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("host")
    ap.add_argument("--port", type=int, default=2323)
    ap.add_argument("--password", default="")
    ap.add_argument("--hours", type=float, default=48.0)
    ap.add_argument("--outdir", default=None)
    ap.add_argument("--minfree", type=int, default=500, help="stop below N MB free")
    ap.add_argument("--no-debug-flags", action="store_true",
                    help="do not switch the node, only record what it emits")
    ap.add_argument("--stall-timeout", type=float, default=STALL_TIMEOUT,
                    help="seconds of read silence before the connection is "
                         "declared dead and re-opened (TM-50; default %.0f)"
                         % STALL_TIMEOUT)
    args = ap.parse_args()

    outdir = args.outdir or os.path.expanduser("~/meshlog/" + args.host.split(".")[0])
    sink = Sink(outdir)
    status_path = os.path.join(outdir, "status.txt")
    pause_path = os.path.join(outdir, "PAUSE")

    signal.signal(signal.SIGTERM, on_signal)
    signal.signal(signal.SIGINT, on_signal)

    started = time.time()
    deadline = started + args.hours * 3600.0
    console = Console(args.host, args.port, args.password)

    flags_set = False
    prev_loradebug = "off"
    reconnects = 0
    last_status = 0.0
    backoff = BACKOFF_MIN
    reason = "running"

    sink.write("[LOGGER] start host=%s hours=%.1f outdir=%s" % (args.host, args.hours, outdir))

    while not stop_requested and time.time() < deadline:
        try:
            rest = console.connect()
            backoff = BACKOFF_MIN
            sink.write("[LOGGER] connected to %s:%d" % (args.host, args.port))

            if not flags_set and not args.no_debug_flags:
                # Record the state BEFORE switching so it can be restored at
                # the end. The --info reply is logged too; since N-31 the
                # firmware masks the passwords in it itself.
                console.send("--info")
                info = b""
                probe_end = time.time() + 4.0
                while time.time() < probe_end:
                    try:
                        chunk = console.sock.recv(4096)
                    except socket.timeout:
                        break
                    if not chunk:
                        break
                    info += chunk
                rest += info
                m = re.search(rb"\.\.\.LORADEBUG (on|off)", info)
                prev_loradebug = m.group(1).decode() if m else "off"

                console.send("--txcapture on")
                time.sleep(1.0)
                console.send("--loradebug on")
                flags_set = True
                sink.write("[LOGGER] flags set (loradebug was: %s)" % prev_loradebug)
            elif flags_set and not args.no_debug_flags:
                # TM-50: the flags live in flash and survive a node reboot,
                # but a reconnect can also follow paths where they do not
                # (settings restored from backup, a fresh flash mid-run).
                # Both commands are idempotent, so re-applying is free and
                # keeps the recording verbose no matter why we reconnected.
                console.send("--txcapture on")
                time.sleep(1.0)
                console.send("--loradebug on")
                sink.write("[LOGGER] flags re-applied after reconnect")

            buf = rest
            last_data = time.time()
            while not stop_requested and time.time() < deadline:
                # recv() == b"" means the peer closed (node rebooted, WiFi
                # gone). A timeout only means "quiet right now". Both yield an
                # empty chunk, so they are told apart by the exception.
                closed = False
                try:
                    chunk = console.sock.recv(4096)
                    if chunk == b"":
                        closed = True
                except socket.timeout:
                    chunk = b""

                if closed:
                    raise ConnectionError("peer closed the connection")

                if not chunk and time.time() - last_data > args.stall_timeout:
                    # TM-50: recv() timeouts alone never end -- a peer that
                    # died without a FIN (node WLAN outage) leaves a half-open
                    # socket that times out forever. Silence beyond the
                    # watchdog is treated as a dead connection; the normal
                    # reconnect path (close, backoff, re-apply flags) takes
                    # over and counts it in reconnects=.
                    raise ConnectionError(
                        "no data for %.0f s -- assuming zombie connection"
                        % (time.time() - last_data))

                if chunk:
                    last_data = time.time()
                    buf += chunk
                    while b"\n" in buf:
                        line, _, buf = buf.partition(b"\n")
                        sink.write(line.decode(errors="replace").rstrip("\r"))
                    if len(buf) > 65536:        # no newline in 64 KB -- drop it
                        sink.write("[LOGGER] oversized line discarded (%d B)" % len(buf))
                        buf = b""

                if os.path.exists(pause_path):
                    sink.write("[LOGGER] PAUSE found -- releasing the console")
                    console.close()
                    reason = "paused"
                    while os.path.exists(pause_path) and not stop_requested and time.time() < deadline:
                        time.sleep(10)
                    reason = "running"
                    sink.write("[LOGGER] PAUSE lifted -- reconnecting")
                    raise ConnectionError("pause")

                now = time.time()
                if now - last_status >= 60:
                    last_status = now
                    free_now = free_mb(outdir)
                    with open(status_path, "w") as fh:
                        fh.write(
                            "pid=%d\nhost=%s\nstart=%s\nnow=%s\n"
                            "elapsed_h=%.2f\nremaining_h=%.2f\n"
                            "lines=%d\nbytes=%d\nreconnects=%d\nfree_mb=%d\nstatus=%s\n"
                            % (os.getpid(), args.host,
                               datetime.fromtimestamp(started).strftime("%Y-%m-%d %H:%M:%S"),
                               datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
                               (now - started) / 3600.0, max(0.0, (deadline - now) / 3600.0),
                               sink.lines, sink.bytes, reconnects, free_now, reason))
                    if free_now < args.minfree:
                        sink.write("[LOGGER] stopping: only %d MB free" % free_now)
                        reason = "stopped: out of disk"
                        raise SystemExit(3)

        except SystemExit:
            raise
        except (OSError, ConnectionError) as exc:
            console.close()
            if stop_requested or time.time() >= deadline:
                break
            if str(exc) == "pause":
                backoff = BACKOFF_MIN
                continue
            reconnects += 1
            sink.write("[LOGGER] connection lost (%s) -- retry in %d s" % (exc, backoff))
            time.sleep(backoff)
            backoff = min(BACKOFF_MAX, backoff * 2)

    # Clean up: restore the flags. TM-50: two attempts -- the first send can
    # hit a zombie socket the run ended on (Broken pipe was exactly how the
    # 2026-09-01 incident surfaced, and the flags stayed on); the second
    # attempt always starts from a fresh connection.
    if flags_set and not args.no_debug_flags:
        for attempt in (1, 2):
            try:
                if console.sock is None:
                    console.connect()
                console.send("--txcapture off")
                time.sleep(1.0)
                console.send("--loradebug " + prev_loradebug)
                time.sleep(2.0)
                sink.write("[LOGGER] flags restored (loradebug %s)" % prev_loradebug)
                break
            except (OSError, ConnectionError) as exc:
                console.close()
                if attempt == 2:
                    sink.write("[LOGGER] flags could NOT be restored: %s" % exc)

    console.close()
    if reason == "running":
        reason = "finished: time elapsed" if not stop_requested else "finished: signal"
    sink.write("[LOGGER] end (%s) lines=%d bytes=%d reconnects=%d"
               % (reason, sink.lines, sink.bytes, reconnects))
    with open(status_path, "w") as fh:
        fh.write("pid=%d\nhost=%s\nlines=%d\nbytes=%d\nreconnects=%d\nstatus=%s\nend=%s\n"
                 % (os.getpid(), args.host, sink.lines, sink.bytes, reconnects, reason,
                    datetime.now().strftime("%Y-%m-%d %H:%M:%S")))
    sink.close()


if __name__ == "__main__":
    main()
