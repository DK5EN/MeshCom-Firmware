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

Time zone: the prefix is the wall clock of the capture zone, ``--tz local``
(default, the host zone), ``--tz utc`` or an IANA name such as
``Europe/Vienna``. The prefix format never changes (every harvester anchors on
it); the zone is named instead: each file opens with
``[LOGGER] tz=<zone> utc_offset=+HH:MM utc=<ISO Z>``, a DST jump writes a
``utc_offset changed`` line, the day files roll over on the capture zone's
date, and ``status.txt`` carries ``tz=``, ``utc_offset=`` and ISO start/now.

Volume is roughly 200 B/s with both debug flags on, so about 36 MB for 48 h.

Usage::

    uv run tools/meshlogger.py dk5en-98.local --hours 48
    uv run tools/meshlogger.py 192.168.68.66 --call DK5EN-92 --hours 24   # MC_PW_DK5EN_92 from .env

Short passive captures (send nothing but ``--cmd``, one file)::

    uv run tools/meshlogger.py dk5en-14.local --no-debug-flags --seconds 75 --out /tmp/con.log
    uv run tools/meshlogger.py dk5en-14.local --no-debug-flags --seconds 12 --out /tmp/info.log --cmd=--info

Unattended, on a machine that stays up (screen survives the SSH session, but
not a reboot -- use a systemd unit if the run has to outlive one)::

    screen -dmS meshlog python3 meshlogger.py dk5en-98.local --hours 48 \
        --outdir ~/meshlog/dk5en-98
    screen -r meshlog            # attach, Ctrl-A D to detach
    touch ~/meshlog/dk5en-98/PAUSE     # release the console
    rm ~/meshlog/dk5en-98/PAUSE        # hand it back

Only the standard library is used, so it runs on a Raspberry Pi Zero with the
system Python and no virtualenv. Deploy it together with ``mc_console.py``
(login and password lookup); on the Pi the password comes from the
environment or ``MC_ENV_FILE``, as there is no repo ``.env``.
"""

import argparse
import os
import re
import shutil
import signal
import socket
import sys
import time
from datetime import datetime, timezone
from zoneinfo import ZoneInfo

# mc_console.py sits next to this file, in the repo and on the rpizero alike.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mc_console  # noqa: E402

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

#: Flags forced on for the run and restored at exit. NBR-01: the NBR duty-cycle
#: test wants only ``--nbrdebug on`` (the two default flags flood the log at
#: ~200 B/s and would swamp the [NBR] lines) -- ``--flags nbrdebug`` replaces
#: this list, it does not add to it.
DEFAULT_FLAGS = "txcapture,loradebug"

stop_requested = False


def parse_flag_state(info, flag):
    """Looks for "...<FLAG> on/off" in a ``--info`` reply (LORADEBUG style).

    Returns ``"on"``/``"off"``, or ``None`` when the node's ``--info`` does
    not report this flag at all -- the caller must then leave the flag alone
    at exit instead of guessing a value that could turn ON something that
    was never touched.
    """
    m = re.search(rb"\.\.\." + flag.upper().encode() + rb" (on|off)", info)
    return m.group(1).decode() if m else None


def on_signal(signum, frame):
    global stop_requested
    stop_requested = True


def resolve_tz(spec):
    """``local`` (or None) -> None, the host zone; ``utc`` -> UTC; else an IANA name."""
    if spec is None or spec.lower() == "local":
        return None
    if spec.lower() == "utc":
        return timezone.utc
    return ZoneInfo(spec)


def local_zone_name():
    """IANA name of the host zone where the system states it, else its abbreviation."""
    tzenv = os.environ.get("TZ", "").lstrip(":")
    if "/" in tzenv:
        return tzenv
    try:
        with open("/etc/timezone") as fh:
            name = fh.read().strip()
        if name:
            return name
    except OSError:
        pass
    link = os.path.realpath("/etc/localtime")
    if "zoneinfo/" in link:
        return link.split("zoneinfo/", 1)[1]
    return datetime.now().astimezone().tzname() or "local"


def tz_label(tz):
    if tz is None:
        return local_zone_name()
    if tz is timezone.utc:
        return "UTC"
    return str(tz)


def now_in(tz):
    """Aware 'now' in the capture zone (tz None = host zone)."""
    return datetime.now(tz) if tz is not None else datetime.now().astimezone()


def at(ts, tz):
    """Aware datetime for a time.time() value in the capture zone."""
    return datetime.fromtimestamp(ts, tz) if tz is not None else datetime.fromtimestamp(ts).astimezone()


def stamp(dt):
    """Line prefix: wall clock of ``dt``. Format is fixed -- the harvesters anchor on it."""
    return dt.strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]


def utc_offset(dt):
    z = dt.strftime("%z")
    return z[:3] + ":" + z[3:]


def utc_iso(dt):
    return dt.astimezone(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3] + "Z"


class Sink:
    """One log file per day (or the one ``outfile``), one timestamped line per console line.

    Every file opened gets a ``[LOGGER] tz=...`` header; a change of the UTC offset
    (DST) gets a marker line, so each prefix can be put on UTC without guessing.
    """

    def __init__(self, outdir, outfile=None, tz=None, clock=None):
        self.outdir = outdir
        self.outfile = outfile
        self.tz = tz
        self.clock = clock or (lambda: now_in(tz))
        self.label = tz_label(tz)
        self.offset = None
        self.day = None
        self.fh = None
        self.lines = 0
        self.bytes = 0
        os.makedirs(outdir, exist_ok=True)

    def _emit(self, dt, text):
        line = "%s  %s\n" % (stamp(dt), text)
        self.fh.write(line)
        self.lines += 1
        self.bytes += len(line)

    def _open(self, path, dt):
        # Append: restarting the logger must not discard the day.
        self.fh = open(path, "a", buffering=1, errors="replace")
        self.offset = utc_offset(dt)
        self._emit(dt, "[LOGGER] tz=%s utc_offset=%s utc=%s -- line times are %s wall clock"
                   % (self.label, self.offset, utc_iso(dt), self.label))

    def _rotate(self, dt):
        if self.outfile:
            if not self.fh:
                self._open(self.outfile, dt)
            return
        day = dt.strftime("%Y-%m-%d")
        if day != self.day:
            if self.fh:
                self.fh.close()
            self.day = day
            self._open(os.path.join(self.outdir, day + ".log"), dt)

    def write(self, text):
        dt = self.clock()
        self._rotate(dt)
        off = utc_offset(dt)
        if off != self.offset:
            self._emit(dt, "[LOGGER] tz=%s utc_offset changed %s -> %s utc=%s"
                       % (self.label, self.offset, off, utc_iso(dt)))
            self.offset = off
        self._emit(dt, text)

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
    ap.add_argument("--call", default=None,
                    help="callsign at host for the password lookup (MC_PW_<CALL> in "
                         ".env or the environment); default: taken from a host name "
                         "like dk5en-14.local")
    ap.add_argument("--hours", type=float, default=48.0)
    ap.add_argument("--seconds", type=float, default=None,
                    help="run length in seconds, replaces --hours")
    ap.add_argument("--outdir", default=None)
    ap.add_argument("--out", default=None,
                    help="write everything into this one file instead of "
                         "<outdir>/YYYY-MM-DD.log (status.txt and PAUSE live next to it)")
    ap.add_argument("--cmd", action="append", default=[],
                    help="console command to send once after the first connect "
                         "(repeatable), e.g. --cmd=--info")
    ap.add_argument("--minfree", type=int, default=500, help="stop below N MB free")
    ap.add_argument("--no-debug-flags", action="store_true",
                    help="do not switch the node, only record what it emits")
    ap.add_argument("--flags", default=DEFAULT_FLAGS,
                    help="comma-separated node flags to force on for the run and "
                         "restore at exit, replacing the default list entirely "
                         "(default: %(default)s; e.g. --flags nbrdebug for the "
                         "NBR duty-cycle test)")
    ap.add_argument("--stall-timeout", type=float, default=STALL_TIMEOUT,
                    help="seconds of read silence before the connection is "
                         "declared dead and re-opened (TM-50; default %.0f)"
                         % STALL_TIMEOUT)
    ap.add_argument("--tz", default="local",
                    help="zone of the line timestamps: local (host zone), utc, or an IANA "
                         "name like Europe/Vienna; named in every file header "
                         "(default: %(default)s)")
    args = ap.parse_args()
    tz = resolve_tz(args.tz)

    if args.out:
        outdir = os.path.dirname(os.path.abspath(args.out))
    else:
        outdir = args.outdir or os.path.expanduser("~/meshlog/" + args.host.split(".")[0])
    sink = Sink(outdir, args.out, tz=tz)
    status_path = os.path.join(outdir, "status.txt")
    pause_path = os.path.join(outdir, "PAUSE")

    signal.signal(signal.SIGTERM, on_signal)
    signal.signal(signal.SIGINT, on_signal)

    started = time.time()
    run_s = args.seconds if args.seconds is not None else args.hours * 3600.0
    deadline = started + run_s
    console = mc_console.Console(args.host, args.port, call=args.call,
                                 connect_timeout=CONNECT_TIMEOUT)

    flag_names = [f.strip() for f in args.flags.split(",") if f.strip()]
    flags_set = False
    prev_state = {}  # flag name -> "on"/"off"/None (None = unknown, leave alone at exit)
    reconnects = 0
    last_status = 0.0
    backoff = BACKOFF_MIN
    reason = "running"

    sink.write("[LOGGER] start host=%s hours=%.1f outdir=%s" % (args.host, run_s / 3600.0, outdir))
    cmds_sent = False

    while not stop_requested and time.time() < deadline:
        try:
            rest = console.connect()
            console.sock.settimeout(READ_TIMEOUT)
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
                for name in flag_names:
                    prev_state[name] = parse_flag_state(info, name)
                    if prev_state[name] is None:
                        sink.write(
                            "[LOGGER] flag %s: state not found in --info -- "
                            "will be left untouched at exit" % name)

                for i, name in enumerate(flag_names):
                    console.send("--%s on" % name)
                    if i < len(flag_names) - 1:
                        time.sleep(1.0)
                flags_set = True
                sink.write("[LOGGER] flags set (%s)" % ", ".join(
                    "%s was %s" % (n, prev_state[n] if prev_state[n] is not None else "unknown")
                    for n in flag_names))
            elif flags_set and not args.no_debug_flags:
                # TM-50: the flags live in flash and survive a node reboot,
                # but a reconnect can also follow paths where they do not
                # (settings restored from backup, a fresh flash mid-run).
                # Every command here is idempotent, so re-applying is free and
                # keeps the recording verbose no matter why we reconnected.
                for i, name in enumerate(flag_names):
                    console.send("--%s on" % name)
                    if i < len(flag_names) - 1:
                        time.sleep(1.0)
                sink.write("[LOGGER] flags re-applied after reconnect")

            if not cmds_sent:
                for i, cmd in enumerate(args.cmd):
                    if i:
                        time.sleep(1.0)
                    console.send(cmd)
                    sink.write("[LOGGER] >> %s" % cmd)
                cmds_sent = True

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
                            "tz=%s\nutc_offset=%s\nstart_iso=%s\nnow_iso=%s\n"
                            "elapsed_h=%.2f\nremaining_h=%.2f\n"
                            "lines=%d\nbytes=%d\nreconnects=%d\nfree_mb=%d\nstatus=%s\n"
                            % (os.getpid(), args.host,
                               at(started, tz).strftime("%Y-%m-%d %H:%M:%S"),
                               at(now, tz).strftime("%Y-%m-%d %H:%M:%S"),
                               sink.label, utc_offset(at(now, tz)),
                               at(started, tz).isoformat(timespec="seconds"),
                               at(now, tz).isoformat(timespec="seconds"),
                               (now - started) / 3600.0, max(0.0, (deadline - now) / 3600.0),
                               sink.lines, sink.bytes, reconnects, free_now, reason))
                    if free_now < args.minfree:
                        sink.write("[LOGGER] stopping: only %d MB free" % free_now)
                        reason = "stopped: out of disk"
                        raise SystemExit(3)

        except SystemExit:
            raise
        except (mc_console.LoginError, mc_console.EnvFileError) as exc:
            sink.write("[LOGGER] stopping: %s" % exc)
            reason = "stopped: login refused"
            break
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
        restore = [(n, v) for n, v in prev_state.items() if v is not None]
        left_alone = [n for n, v in prev_state.items() if v is None]
        # txcapture defaulted to an unconditional "off" here before this flag
        # list was generalised; a flag whose prior state was never found in
        # --info is now left untouched instead, so a state we never actually
        # observed is never guessed at shutdown.
        for attempt in (1, 2):
            try:
                if console.sock is None:
                    console.connect()
                for name, val in restore:
                    console.send("--%s %s" % (name, val))
                    time.sleep(1.0)
                time.sleep(1.0)
                if restore:
                    sink.write("[LOGGER] flags restored (%s)" % ", ".join(
                        "%s=%s" % (n, v) for n, v in restore))
                if left_alone:
                    sink.write("[LOGGER] flags left untouched (unknown prior state): %s"
                               % ", ".join(left_alone))
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
        end = now_in(tz)
        fh.write("pid=%d\nhost=%s\nlines=%d\nbytes=%d\nreconnects=%d\nstatus=%s\nend=%s\n"
                 "tz=%s\nutc_offset=%s\nend_iso=%s\n"
                 % (os.getpid(), args.host, sink.lines, sink.bytes, reconnects, reason,
                    end.strftime("%Y-%m-%d %H:%M:%S"), sink.label, utc_offset(end),
                    end.isoformat(timespec="seconds")))
    sink.close()


if __name__ == "__main__":
    main()
