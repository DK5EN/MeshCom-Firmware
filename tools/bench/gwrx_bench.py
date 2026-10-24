"""GWRX-01 bench driver: bench gateways against the mock server, --nbrrelay count.

Holds one console per node (TCP 2323 via mc_console, or USB serial for nodes
without a net console such as the RAK4631), logs every line, configures the
mock override, sends server text frames (flag 0x80, so no real gateway uploads
them) through the mock, and restores the settings in a finally block.

Default flow per node (when no --setup-cmd is given for it):
  setup:   --srvip <mock-host>, --nbrrelay count, --nbrdebug on, --gateway on
  restore: --gateway off, --srvip 0.0.0.0 (always first, even with --restore-cmd),
           then --nbrdebug <value read via --info>, --nbrrelay <value read via
           --info>, and --gateway on if --info reported it on

Safety: --gateway on is PERSISTED to flash while --srvip is RAM only. A gateway
that reboots (or is left behind) therefore talks to the REAL server. The driver
watches every node for a boot marker, sends --gateway off at once on a reboot,
aborts the run, and loudly reports any node it could not restore.

Example:
  python3 tools/bench/gwrx_bench.py --node DK5EN-1=tcp:dk5en-1.local \\
      --node DK5EN-90=serial:230D6EBB3266D20E --mock-host <mac-ip> --frames 3

Passwords never come from the command line: TCP nodes use MC_PW_<CALL> from .env.
"""

from __future__ import annotations

import argparse
import contextlib
import ipaddress
import re
import signal
import socket
import sys
import threading
import time
from pathlib import Path
from typing import Callable, Iterator, NamedTuple, Optional, Protocol

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(REPO / "tools"))
sys.path.insert(0, str(REPO / "tools" / "mock"))
sys.path.insert(0, str(HERE))

import gwrx_frame  # noqa: E402

try:
    import serial
    from serial.tools import list_ports
except ImportError:  # pragma: no cover - only serial nodes need it
    serial = None  # type: ignore[assignment]
    list_ports = None  # type: ignore[assignment]

DEFAULT_LOG_DIR = REPO / "tools" / "bench" / "runs" / "harness"
DEFAULT_SOURCE = "DK5EN-97"
MOCK_GATEWAY_ID = 0x4D4F434B

OWN_CALL_RE = re.compile(r"DK5EN-\d+")
ALLOWED_GROUPS = ("9", "9999", "TEST")
# ESP32 ROM banner, firmware start banner ([BOOT];... / [BOOT] RESETREAS=...).
BOOT_RE = re.compile(r"\[BOOT\]|\brst:0x|^ets \w|ESP-ROM:")
SAFETY_RESTORE = ["--gateway off", "--srvip 0.0.0.0"]

# Timing knobs, module level so the tests can shrink them.
CMD_WAIT_SCALE = 1.0  # multiplies every post-command wait
SRVIP_TIMEOUT = 10.0  # wait for the [SRVIP];<ip>;set confirmation
REBOOT_SETTLE = 8.0  # let a rebooted node come back before restoring it
# A reboot over 2323 shows only as a dropped socket (the banner goes to USB): keep
# reconnecting this long, so the persisted gateway flag can be switched off.
LINK_LOST_RECONNECT_S = 90.0
LINK_LOST_RETRY_S = 3.0
# connect timeout + login of one mc_console attempt (tools/mc_console.py), for the quiesce wait
CONSOLE_ATTEMPT_S = 20.0
TAIL_WAIT_S = 20.0  # after the last frame: let relays, cancels and the NBR lines land
# Tight TCP keepalive on the 2323 socket: a rebooted ESP32 sends no FIN, but answers the
# first probe with RST, so a silent reboot surfaces within ~KEEPIDLE + KEEPINTVL seconds.
TCP_KEEPIDLE_S, TCP_KEEPINTVL_S, TCP_KEEPCNT = 5, 2, 3
# Commands a setup or restore must never contain (reboot drops the RAM-only srvip;
# --gateway srv reboots a gateway node, command_functions.cpp --gateway srv).
FORBIDDEN_CMDS = (("--reboot",), ("--gateway", "srv"))


def norm(cmd: str) -> list[str]:
    """Command words, lower case: the firmware matches commands case-insensitively."""
    return cmd.strip().lower().split()


class PlanError(ValueError):
    """Invalid or unsafe run configuration."""


class SetupError(RuntimeError):
    """Setup failed; the run stops and restores."""


class RunAborted(RuntimeError):
    """A node rebooted during the run; the run stops and restores."""


# ------------------------------------------------------------------ transports


class Transport(Protocol):
    def connect(self) -> None: ...

    def reconnect(self) -> None: ...

    def send_line(self, line: str) -> None: ...

    def readline(self, timeout: float) -> Optional[str]:
        """Next line, None on timeout; raises EOFError when the link is gone."""
        ...

    def close(self) -> None: ...


class TcpTransport:
    def __init__(self, host: str, call: str) -> None:
        import mc_console

        self.con = mc_console.Console(host, call=call)
        self._buf = b""
        self._closed = False

    def connect(self) -> None:
        self.con.connect()
        self._tight_keepalive()

    def reconnect(self) -> None:
        self._buf = b""
        self._closed = False
        self.con.connect()
        self._tight_keepalive()

    def _tight_keepalive(self) -> None:
        sock = self.con.sock
        if sock is None:
            return
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
        for opt, val in (
            ("TCP_KEEPIDLE", TCP_KEEPIDLE_S),
            ("TCP_KEEPALIVE", TCP_KEEPIDLE_S),  # macOS name of the idle knob
            ("TCP_KEEPINTVL", TCP_KEEPINTVL_S),
            ("TCP_KEEPCNT", TCP_KEEPCNT),
        ):
            if hasattr(socket, opt):
                try:
                    sock.setsockopt(socket.IPPROTO_TCP, getattr(socket, opt), val)
                except OSError:
                    pass

    def send_line(self, line: str) -> None:
        self.con.send(line)

    def readline(self, timeout: float) -> Optional[str]:
        deadline = time.monotonic() + timeout
        while b"\n" not in self._buf:
            sock = self.con.sock
            if self._closed or sock is None:
                raise EOFError("closed")
            if time.monotonic() >= deadline:
                return None
            try:
                d = sock.recv(4096)
            except TimeoutError:
                continue
            except OSError as e:
                raise EOFError(str(e)) from e
            if not d:
                raise EOFError("peer closed")
            self._buf += d
        raw, self._buf = self._buf.split(b"\n", 1)
        return raw.decode("utf-8", "replace").rstrip("\r")

    def close(self) -> None:
        self._closed = True
        self.con.close()


def resolve_serial_port(serial_number: str) -> str:
    """Find the /dev port by USB serial number (never by device name)."""
    if list_ports is None:
        raise PlanError("pyserial is not installed (uv run --with pyserial)")
    ports = list(list_ports.comports())
    for p in ports:
        if p.serial_number and p.serial_number.upper() == serial_number.upper():
            return p.device
    seen = ", ".join(sorted(str(p.serial_number) for p in ports if p.serial_number)) or "none"
    raise PlanError(f"no USB serial port with serial number {serial_number} (seen: {seen})")


class SerialTransport:
    def __init__(self, serial_number: str, baud: int = 115200) -> None:
        self.serial_number, self.baud = serial_number, baud
        self.port: Optional[str] = None
        self.ser = None
        self._buf = b""

    def connect(self) -> None:
        if serial is None:
            raise PlanError("pyserial is not installed (uv run --with pyserial)")
        self.port = resolve_serial_port(self.serial_number)
        self.ser = serial.Serial()
        self.ser.port, self.ser.baudrate, self.ser.timeout = self.port, self.baud, 0.3
        self.ser.exclusive = True  # refuse a port serial_capture / Web Serial holds
        self.ser.dtr = True  # RAK4631 stays mute without DTR; set before open
        self.ser.open()
        self.ser.dtr = True

    def reconnect(self) -> None:
        self.close()
        self._buf = b""
        self.connect()

    def send_line(self, line: str) -> None:
        assert self.ser is not None, "send_line() before connect()"
        self.ser.write(line.encode() + b"\n")

    def readline(self, timeout: float) -> Optional[str]:
        deadline = time.monotonic() + timeout
        while b"\n" not in self._buf:
            if self.ser is None:
                raise EOFError("closed")
            if time.monotonic() >= deadline:
                return None
            try:
                d = self.ser.read(256)
            except (OSError, ValueError) as e:  # ValueError: port closed under us
                raise EOFError(str(e)) from e
            self._buf += d
        raw, self._buf = self._buf.split(b"\n", 1)
        return raw.decode("utf-8", "replace").rstrip("\r")

    def close(self) -> None:
        ser, self.ser = self.ser, None
        if ser is not None:
            try:
                ser.close()
            except OSError:
                pass


# ------------------------------------------------------------------------ node


class Node:
    def __init__(self, call: str, transport: Transport, log_path: Optional[Path] = None) -> None:
        self.call, self.transport = call, transport
        self.lines: list[str] = []
        self.log = open(log_path, "w") if log_path else None
        self._cv = threading.Condition()
        self._stop = threading.Event()
        self._thread: Optional[threading.Thread] = None
        self._gen = 0  # bumped by reconnect(); a stale reader exits
        self.reconnect_failed = False
        self.link_lost = False  # reader saw EOF while the guard was armed
        self.emergency_idle = threading.Event()  # cleared while an on_reboot callback runs
        self.emergency_idle.set()
        self._io = threading.RLock()  # one sender/reconnector per transport at a time
        self._guard = threading.Lock()  # watch_boot / on_reboot vs. _fire_reboot
        self.reconnect_count = 0  # restore redoes its safety steps when this moves
        # Reboot guard: armed by the run once setup is about to begin.
        self.watch_boot = False
        self.rebooted = threading.Event()
        self.boot_line: Optional[str] = None
        self.gateway_armed = False  # set before a --gateway on is sent
        self.on_reboot: Optional[Callable[[Node, str], None]] = None
        self.abort_event: Optional[threading.Event] = None

    def _write_log(self, text: str) -> None:
        if self.log and not self.log.closed:
            self.log.write(f"{time.strftime('%H:%M:%S')} {text}\n")
            self.log.flush()

    def start(self) -> None:
        self.transport.connect()
        self._start_reader()

    def _start_reader(self) -> None:
        self._thread = threading.Thread(target=self._reader, args=(self._gen,), daemon=True)
        self._thread.start()

    def _reader(self, gen: int) -> None:
        while not self._stop.is_set() and gen == self._gen:
            try:
                line = self.transport.readline(0.5)
            except EOFError:
                # Not our own close() or reconnect(): over 2323 a reboot looks exactly like this.
                if self.watch_boot and not self._stop.is_set() and gen == self._gen:
                    self.link_lost = True
                    self._fire_reboot("link lost (suspected reboot)")
                break
            if line is None or gen != self._gen:
                continue
            with self._cv:
                self.lines.append(line)
                self._write_log(line)
                self._cv.notify_all()
            if self.watch_boot and BOOT_RE.search(line):
                self._fire_reboot(line)

    def _fire_reboot(self, line: str) -> None:
        with self._guard:
            if not self.watch_boot or self.rebooted.is_set():
                return
            self.boot_line = line
            self.rebooted.set()
            cb = self.on_reboot
            if cb is None:
                return
            self.emergency_idle.clear()
        try:
            cb(self, line)
        except Exception as e:  # never kill the reader
            self._write_log(f"!!! on_reboot failed: {e}")
        finally:
            self.emergency_idle.set()

    def quiesce_guard(self, timeout: float) -> None:
        """Restore owns the link from here: no new reboot callback, wait for a running one."""
        with self._guard:
            self.watch_boot = False
            self.on_reboot = None
        self.emergency_idle.wait(timeout)

    def link_ok(self) -> bool:
        return not self.link_lost and self._thread is not None and self._thread.is_alive()

    def send_raw(self, line: str) -> None:
        with self._io:
            self.transport.send_line(line)

    def emergency_reconnect(self, budget_s: float, retry_s: float) -> bool:
        """Reconnect until it works or the budget is spent (node still booting); fresh reader."""
        deadline = time.monotonic() + budget_s
        while not self._stop.is_set():
            with self._io:
                try:
                    self._gen += 1  # the old reader (if any) exits
                    try:
                        self.transport.close()
                    except Exception:
                        pass
                    self.transport.reconnect()
                except Exception as e:
                    err = e
                else:
                    self.link_lost = False
                    self.reconnect_failed = False
                    self.reconnect_count += 1
                    self._start_reader()
                    return True
            if time.monotonic() >= deadline:
                self.reconnect_failed = True
                self._write_log(f"!!! emergency reconnect gave up: {err}")
                return False
            self._stop.wait(retry_s)
        return False

    def reconnect(self) -> None:
        """One transport reconnect plus a fresh reader; raises when it fails."""
        if self.reconnect_failed:
            raise ConnectionError(f"{self.call}: link is down (reconnect already failed)")
        self._write_log("!!! reconnecting")
        with self._io:
            self._gen += 1
            try:
                self.transport.close()
            except Exception:
                pass
            try:
                self.transport.reconnect()
            except Exception:
                self.reconnect_failed = True
                raise
            self.link_lost = False
            self.reconnect_count += 1
            self._start_reader()

    def cmd(self, c: str, wait: float = 1.5, restore: bool = False) -> None:
        """Send c and wait. restore=True: ignore the run abort, reconnect once on a
        failed send and resend."""
        if not restore and self.abort_event is not None and self.abort_event.is_set():
            raise RunAborted(f"{self.call}: run aborted before {c!r}")
        if restore and self.reconnect_failed:
            raise ConnectionError(f"{self.call}: link is down, {c!r} not sent")
        self._write_log(f">>> {c}")
        try:
            self.send_raw(c)
        except Exception as e:
            if not restore:
                raise
            self._write_log(f"!!! send failed ({e}); one reconnect, then resend")
            self.reconnect()
            self.send_raw(c)
        self.pause(wait * CMD_WAIT_SCALE)

    def pause(self, secs: float) -> None:
        """Wait up to secs; returns early only when the node is stopped."""
        self._stop.wait(secs)

    def wait_for(self, pat: str, start: int, timeout: float) -> Optional[str]:
        rx = re.compile(pat)
        deadline = time.monotonic() + timeout
        seen = start
        with self._cv:
            while True:
                for ln in self.lines[seen:]:
                    if rx.search(ln):
                        return ln
                seen = len(self.lines)
                left = deadline - time.monotonic()
                if left <= 0:
                    return None
                self._cv.wait(left)

    def close(self) -> None:
        self._stop.set()
        try:
            self.transport.close()
        finally:
            if self._thread:
                self._thread.join(timeout=3)
            if self.log:
                self.log.close()


# ------------------------------------------------------------------------ plan


def parse_node(spec: str) -> tuple[str, str, str]:
    """CALL=tcp:<host> | CALL=serial:<usb-serial>  ->  (call, kind, value)."""
    call, sep, transport = spec.partition("=")
    kind, sep2, value = transport.partition(":")
    if not (sep and sep2 and call and value) or kind not in ("tcp", "serial"):
        raise PlanError(
            f"bad --node {spec!r}: expected CALL=tcp:<host> or CALL=serial:<usb-serial>"
        )
    if not call.upper().startswith("DK5EN-"):
        raise PlanError(f"bad --node {spec!r}: only DK5EN-* callsigns are allowed on the bench")
    return call.upper(), kind, value


def parse_call_cmds(specs: list[str], what: str) -> dict[str, list[str]]:
    out: dict[str, list[str]] = {}
    for s in specs:
        call, sep, cmd = s.partition("=")
        if not sep or not call or not cmd.strip():
            raise PlanError(f"bad {what} {s!r}: expected CALL=CMD")
        out.setdefault(call.upper(), []).append(cmd.strip())
    return out


def check_group(group: str) -> None:
    """Test traffic only: group 9, 9999, TEST, or a DM to an own DK5EN-<n> node."""
    g = group.strip().upper()
    if g in ALLOWED_GROUPS or OWN_CALL_RE.fullmatch(g):
        return
    raise PlanError(
        f"refusing group {group!r}: only 9, 9999, TEST or an own DK5EN-<n> call (DM) are allowed"
    )


def check_source_call(call: str) -> None:
    if not OWN_CALL_RE.fullmatch(call.strip().upper()):
        raise PlanError(f"refusing --source-call {call!r}: must be an own DK5EN-<n> callsign")


def srvip_arg(cmd: str) -> Optional[str]:
    """Argument of a --srvip command (None for any other command or a missing one)."""
    parts = norm(cmd)
    if parts[:1] == ["--srvip"] and len(parts) >= 2:
        return parts[1]
    return None


def is_real_ipv4(text: str) -> bool:
    try:
        return str(ipaddress.IPv4Address(text)) != "0.0.0.0"
    except ValueError:
        return False


def same_ipv4(a: str, b: Optional[str]) -> bool:
    try:
        return b is not None and ipaddress.IPv4Address(a) == ipaddress.IPv4Address(b)
    except ValueError:
        return False


def check_forbidden(call: str, cmds: list[str], what: str) -> None:
    for c in cmds:
        parts = norm(c)
        for bad in FORBIDDEN_CMDS:
            if tuple(parts[: len(bad)]) == bad:
                raise PlanError(
                    f"{call}: {what} command {c!r} is not allowed (reboots / drops srvip)"
                )


def check_gateway_srvip(call: str, cmds: list[str], mock_host: Optional[str] = None) -> None:
    """A gateway without the mock override talks to the real server: every --gateway on
    needs an effective --srvip before it, and that srvip must be --mock-host."""
    srvip: Optional[str] = None
    gateway_on = False
    for c in cmds:
        parts = norm(c)
        if parts[:1] == ["--srvip"]:
            arg = srvip_arg(c)
            if gateway_on and not (arg is not None and same_ipv4(arg, mock_host)):
                raise PlanError(
                    f"{call}: {c!r} after '--gateway on' would move an armed gateway off the mock"
                )
            srvip = arg if arg is not None and is_real_ipv4(arg) else None  # 0.0.0.0 resets
        if parts[:2] == ["--gateway", "on"]:
            if srvip is None:
                raise PlanError(
                    f"{call}: '--gateway on' without an effective --srvip <IPv4> before it "
                    "(missing, not an IPv4 address, or reset by --srvip 0.0.0.0)"
                )
            if not same_ipv4(srvip, mock_host):
                raise PlanError(
                    f"{call}: '--gateway on' after --srvip {srvip}, which is not --mock-host "
                    f"{mock_host!r}"
                )
            gateway_on = True


def check_restore_cmds(call: str, cmds: list[str]) -> None:
    """The default tail owns the gateway flag and the srvip: a custom restore may not set them."""
    check_forbidden(call, cmds, "restore")
    for c in cmds:
        parts = norm(c)
        if parts[:2] == ["--gateway", "on"]:
            raise PlanError(
                f"{call}: --restore-cmd may not contain {c!r}; omit --restore-cmd "
                "to have the original gateway state restored"
            )
        arg = srvip_arg(c)
        if arg is not None and arg != "0.0.0.0":
            raise PlanError(f"{call}: --restore-cmd may not set --srvip {arg}")


class InfoState(NamedTuple):
    """What --info reported; None for a line that was not there."""

    nbrrelay: Optional[str]
    gateway: Optional[str]
    nbrdebug: Optional[str]


def parse_info_state(lines: list[str]) -> InfoState:
    relay = gw = dbg = None
    for ln in lines:
        relay = relay or re.search(r"NBRRELAY (on|count|off)", ln)
        gw = gw or re.search(r"/ Gateway (on|off)", ln)
        dbg = dbg or re.search(r"NBRDEBUG (on|off)", ln)
    return InfoState(
        relay.group(1) if relay else None,
        gw.group(1) if gw else None,
        dbg.group(1) if dbg else None,
    )


def default_tail(nbrrelay: str, gateway: str, nbrdebug: Optional[str] = None) -> list[str]:
    """Original settings back. nbrdebug comes from the --info line
    '...NBRDEBUG on|off' (src/command_functions.cpp); a missing line falls back
    to forcing it off."""
    cmds = [f"--nbrdebug {nbrdebug or 'off'}", f"--nbrrelay {nbrrelay}"]
    if gateway == "on":
        cmds.append("--gateway on")
    return cmds


def default_restore(nbrrelay: str, gateway: str, nbrdebug: Optional[str] = None) -> list[str]:
    """Gateway off before the mock override is dropped, original gateway state last."""
    return SAFETY_RESTORE + default_tail(nbrrelay, gateway, nbrdebug)


def default_setup(mock_host: str) -> list[str]:
    return [f"--srvip {mock_host}", "--nbrrelay count", "--nbrdebug on", "--gateway on"]


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument(
        "--node",
        action="append",
        default=[],
        metavar="CALL=TRANSPORT",
        help="repeatable; tcp:<host> (2323) or serial:<usb-serial-number>",
    )
    ap.add_argument(
        "--mock-host", help="IP of the mock server (UDP); needed for frames and default setup"
    )
    ap.add_argument("--mock-port", type=int, default=1990)
    ap.add_argument("--frames", type=int, default=3)
    ap.add_argument("--interval", type=float, default=45.0, help="seconds between frames")
    ap.add_argument(
        "--warmup", type=float, default=60.0, help="seconds between setup and first frame"
    )
    ap.add_argument("--group", default="9999")
    ap.add_argument("--source-call", default=DEFAULT_SOURCE)
    ap.add_argument(
        "--flags", default="A2", help="frame flags byte, hex; must include 0x80 (server bit)"
    )
    ap.add_argument(
        "--setup-cmd",
        action="append",
        default=[],
        metavar="CALL=CMD",
        help="repeatable; replaces the default setup of that node",
    )
    ap.add_argument(
        "--restore-cmd",
        action="append",
        default=[],
        metavar="CALL=CMD",
        help="repeatable; replaces the default restore tail of that node "
        "(--gateway off and --srvip 0.0.0.0 are always sent first)",
    )
    ap.add_argument("--log-dir", type=Path, default=DEFAULT_LOG_DIR)
    ap.add_argument("--tag", default="run")
    return ap


class Plan:
    def __init__(self, args: argparse.Namespace) -> None:
        if not args.node:
            raise PlanError("no --node given")
        check_group(args.group)
        check_source_call(args.source_call)
        args.source_call = args.source_call.strip().upper()
        self.args = args
        self.nodes = [parse_node(n) for n in args.node]
        calls = [c for c, _, _ in self.nodes]
        if len(set(calls)) != len(calls):
            raise PlanError("duplicate node callsign")
        setup = parse_call_cmds(args.setup_cmd, "--setup-cmd")
        restore = parse_call_cmds(args.restore_cmd, "--restore-cmd")
        for c in list(setup) + list(restore):
            if c not in calls:
                raise PlanError(f"command given for unknown node {c}")
        self.default_restore: dict[str, bool] = {}
        self.setup: dict[str, list[str]] = {}
        self.restore: dict[str, list[str]] = {}
        for c in calls:
            if c in setup:
                self.setup[c] = setup[c]
            else:
                if not args.mock_host:
                    raise PlanError(f"{c}: default setup needs --mock-host (or give --setup-cmd)")
                self.setup[c] = default_setup(args.mock_host)
            self.restore[c] = restore.get(c, [])
            self.default_restore[c] = c not in restore
            check_forbidden(c, self.setup[c], "setup")
            check_gateway_srvip(c, self.setup[c], args.mock_host)
            check_restore_cmds(c, self.restore[c])
        if args.frames > 0 and not args.mock_host:
            raise PlanError("--frames > 0 needs --mock-host")
        try:
            self.flags = int(args.flags, 16)
        except ValueError as e:
            raise PlanError(f"bad --flags {args.flags!r}") from e
        if not 0 <= self.flags <= 0xFF or not self.flags & 0x80:
            raise PlanError(
                f"refusing --flags {args.flags!r}: needs the server bit 0x80 (and one byte) "
                "so no real gateway uploads the frames"
            )

    def restore_cmds(self, call: str, state: InfoState) -> list[str]:
        """Safety steps first, always; then the default tail or the custom commands."""
        if self.default_restore[call]:
            return default_restore(state.nbrrelay or "off", state.gateway or "off", state.nbrdebug)
        return SAFETY_RESTORE + self.restore[call]

    def describe(self) -> str:
        a = self.args
        out = [
            f"plan: {a.frames} frames every {a.interval:g} s after {a.warmup:g} s warm-up, "
            f"group {a.group}, source {a.source_call}, flags 0x{self.flags:02X}, "
            f"mock {a.mock_host or '-'}:{a.mock_port}"
        ]
        for call, kind, value in self.nodes:
            out.append(f"  {call}: {kind}:{value}")
            out += [f"    setup   {c}" for c in self.setup[call]]
            if self.default_restore[call]:
                out.append(
                    "    restore (default) gateway off, srvip 0.0.0.0, "
                    "nbrdebug <orig>, nbrrelay <orig>, gateway <orig>"
                )
            else:
                out += [f"    restore {c}" for c in SAFETY_RESTORE + self.restore[call]]
        return "\n".join(out)


def make_transport(kind: str, value: str, call: str) -> Transport:
    if kind == "tcp":
        return TcpTransport(value, call)
    return SerialTransport(value)


# --------------------------------------------------------------------- summary

NBR_RE = re.compile(
    r"\[NBR\]\|(NEED|CANCEL\?|CANCEL|REFUSE)\|([^|]*)\|([0-9A-Fa-f]{8})\|([^|]*)\|?(.*)"
)


def tally(lines: list[str], ids: list[int]) -> dict[str, dict[str, int]]:
    """Per injected msg_id: NEED A/B/U, CANCEL?, CANCEL, REFUSE, GWI, TXg, heard."""
    out: dict[str, dict[str, int]] = {}
    for mid in ids:
        h = f"{mid:08X}"
        t = dict.fromkeys(("A", "B", "U", "CANCEL?", "CANCEL", "REFUSE", "GWI", "TXg", "heard"), 0)
        for ln in lines:
            m = NBR_RE.search(ln)
            if m and m.group(3).upper() == h:
                kind = m.group(1)
                if kind == "NEED":
                    case = m.group(5).split("|", 1)[0]
                    if case in ("A", "B", "U"):
                        t[case] += 1
                else:
                    t[kind] += 1
            elif f"GWI x{h}" in ln:
                t["GWI"] += 1
            elif re.search(rf"TX x{h}\b.*src=g", ln):
                t["TXg"] += 1
            elif re.search(rf"\[LOG\] \d{{3}} [:@] x{h}\b", ln):
                t["heard"] += 1
        out[h] = t
    return out


def format_summary(call: str, tallies: dict[str, dict[str, int]]) -> str:
    rows = [f"== {call}"]
    for h, t in tallies.items():
        rows.append(
            f"  {h} NEED A/B/U {t['A']}/{t['B']}/{t['U']} CANCEL? {t['CANCEL?']} CANCEL {t['CANCEL']} "
            f"REFUSE {t['REFUSE']} GWI {t['GWI']} TXg {t['TXg']} transmitted {'yes' if t['TXg'] else 'no'} "
            f"heard {t['heard']}"
        )
    return "\n".join(rows)


# ------------------------------------------------------------------------ main


@contextlib.contextmanager
def sigint_ignored() -> Iterator[None]:
    """Ctrl-C must not cut the restore phase short (main thread only)."""
    try:
        old = signal.signal(signal.SIGINT, signal.SIG_IGN)
    except ValueError:  # not the main thread
        yield
        return
    try:
        yield
    finally:
        signal.signal(signal.SIGINT, old)


def new_mock_client(host: str, port: int, callsign: str):
    from mock_client import MockNode

    return MockNode(host, port, gateway_id=MOCK_GATEWAY_ID, callsign=callsign)


def restore_nodes(
    plan: Plan, nodes: list[Node], began: list[str], state: dict[str, InfoState]
) -> bool:
    """Restore every node whose setup began; True only when every node is confirmed.

    Pass 1, safety: --gateway off and --srvip 0.0.0.0 to every node, confirmed by
    `[SRVIP];0.0.0.0;set`; an unconfirmed or failed attempt reconnects and starts
    over from --gateway off (a send into a socket whose peer rebooted can succeed
    and still be lost). Pass 2: the tail. Pass 3: --info must show the expected
    gateway state (and nbrrelay for the default tail). Anything unconfirmed is a
    loud NODE LEFT IN BENCH STATE."""
    todo = [n for n in nodes if n.call in began]
    problems: dict[str, list[str]] = {}
    safe: list[Node] = []
    with sigint_ignored():
        for n in todo:  # restore owns the links now; wait out a running emergency path
            n.quiesce_guard(LINK_LOST_RECONNECT_S + CONSOLE_ATTEMPT_S + LINK_LOST_RETRY_S)
            if not n.emergency_idle.is_set():
                print(f"{n.call}: emergency path still running, restore goes ahead anyway")
        if any(n.rebooted.is_set() for n in todo):
            print(f"reboot seen: waiting {REBOOT_SETTLE:g} s before the restore")
            todo[0].pause(REBOOT_SETTLE)
        for n in todo:
            confirmed = False
            for attempt in (1, 2, 3):
                try:
                    if not n.link_ok() and not n.emergency_reconnect(
                        LINK_LOST_RECONNECT_S, LINK_LOST_RETRY_S
                    ):
                        break
                    rc0, b0 = n.reconnect_count, len(n.lines)
                    n.cmd("--gateway off", 1.5, restore=True)
                    s0 = len(n.lines)
                    n.cmd("--srvip 0.0.0.0", 2, restore=True)
                    got = n.wait_for(r"\[SRVIP\];0\.0\.0\.0;set", s0, SRVIP_TIMEOUT)
                    s1 = len(n.lines)
                    n.cmd("--info", 3, restore=True)
                    gw = parse_info_state(n.lines[s1:]).gateway
                    booted = any(BOOT_RE.search(ln) for ln in n.lines[b0:])
                    if got and gw == "off" and not booted and n.reconnect_count == rc0:
                        confirmed = True
                        break
                    # a reconnect or a reboot in between can have eaten --gateway off: redo
                    print(
                        f"{n.call}: safety not confirmed (attempt {attempt}: srvip "
                        f"{'ok' if got else 'missing'}, gateway {gw}, reboot {booted}, "
                        f"reconnects {n.reconnect_count - rc0})"
                    )
                    if n.link_ok():
                        continue  # link fine: redo the steps on it
                except Exception as e:
                    print(f"{n.call}: safety restore failed (attempt {attempt}): {e!r}")
                n.link_lost = True  # redo from --gateway off on a fresh link
            if confirmed:
                safe.append(n)
            else:
                problems.setdefault(n.call, []).append("safety not confirmed")
        for n in safe:
            for c in plan.restore_cmds(n.call, state[n.call])[len(SAFETY_RESTORE) :]:
                try:
                    n.cmd(c, 1.5, restore=True)
                except Exception as e:
                    print(f"{n.call}: restore {c!r} failed: {e!r}")
                    problems.setdefault(n.call, []).append(f"{c} failed")
        for n in safe:
            want_gw = state[n.call].gateway if plan.default_restore[n.call] else "off"
            want_relay = state[n.call].nbrrelay if plan.default_restore[n.call] else None
            try:
                s0 = len(n.lines)
                n.cmd("--info", 3, restore=True)
                st = parse_info_state(n.lines[s0:])
            except Exception as e:
                problems.setdefault(n.call, []).append(f"--info check failed: {e!r}")
                continue
            if st.gateway != want_gw:
                problems.setdefault(n.call, []).append(f"gateway {st.gateway} (want {want_gw})")
            if want_relay is not None and st.nbrrelay != want_relay:
                problems.setdefault(n.call, []).append(
                    f"nbrrelay {st.nbrrelay} (want {want_relay})"
                )
            if not problems.get(n.call):
                print(f"{n.call}: restored and confirmed (gateway {st.gateway})")
    for call, probs in problems.items():
        bar = "!" * 72
        print(
            f"{bar}\nNODE LEFT IN BENCH STATE: {call} -- check by hand, send --gateway off / "
            f"--srvip 0.0.0.0 if needed\n  {'; '.join(probs)}\n{bar}",
            file=sys.stderr,
        )
    return not problems


def run(plan: Plan, nodes: list[Node], stamp: str) -> int:
    a = plan.args
    state: dict[str, InfoState] = {}
    began: list[str] = []
    ids: list[int] = []
    rc = 0
    completed = False
    abort = threading.Event()
    reasons: list[str] = []

    def on_reboot(n: Node, line: str) -> None:
        msg = f"{n.call}: REBOOT detected ({line.strip()!r})"
        reasons.append(msg)
        print(msg)
        abort.set()
        if not n.gateway_armed:
            return
        # The RAM-only srvip is gone, the persisted gateway flag would hit the real server.
        # restore_nodes() waits on emergency_idle until this callback has returned.
        if n.link_lost and not n.emergency_reconnect(LINK_LOST_RECONNECT_S, LINK_LOST_RETRY_S):
            print(f"{n.call}: link stays down, --gateway off could not be sent")
            return
        print(f"{n.call}: sending --gateway off at once")
        n.send_raw("--gateway off")

    def wait(secs: float) -> None:
        if abort.wait(secs):
            raise RunAborted("; ".join(reasons) or "aborted")

    for n in nodes:
        n.abort_event, n.on_reboot = abort, on_reboot
    try:
        for n in nodes:
            n.start()
            s = len(n.lines)
            n.cmd("--info", 3)
            st = parse_info_state(n.lines[s:])
            missing = [
                k for k, v in (("NBRRELAY", st.nbrrelay), ("Gateway", st.gateway)) if v is None
            ]
            if missing:
                raise SetupError(
                    f"{n.call}: --info has no {' / '.join(missing)} line -- abort before setup"
                )
            if st.nbrdebug is None:
                print(f"{n.call}: --info has no NBRDEBUG line, nbrdebug will be restored to off")
            state[n.call] = st
            if st.gateway == "on":
                print(
                    f"{n.call}: WARNING gateway was ON at start; the restore turns it back on "
                    "(real server). If that is a leftover of a crashed run, abort now."
                )
            print(
                f"{n.call}: nbrrelay was {st.nbrrelay}, gateway was {st.gateway}, "
                f"nbrdebug was {st.nbrdebug or '?'}"
            )
        for n in nodes:
            n.watch_boot = True
        for n in nodes:
            began.append(n.call)  # from here on the node needs a restore
            for c in plan.setup[n.call]:
                if norm(c)[:2] == ["--gateway", "on"]:
                    n.gateway_armed = True
                s = len(n.lines)
                n.cmd(c, 3 if norm(c)[:1] == ["--gateway"] else 1.5)
                arg = srvip_arg(c)
                if arg is not None and arg != "0.0.0.0":
                    try:
                        arg = str(ipaddress.IPv4Address(arg))  # the firmware echoes octets
                    except ValueError:
                        pass
                    ok = n.wait_for(rf"\[SRVIP\];{re.escape(arg)};set", s, SRVIP_TIMEOUT)
                    if not ok:
                        raise SetupError(
                            f"{n.call}: no '[SRVIP];{arg};set' confirmation -- abort before --gateway on"
                        )
                    print(f"{n.call}: {ok.strip()}")
        if a.frames > 0:
            print(f"warm-up {a.warmup:.0f} s")
            wait(a.warmup)
            for n in nodes:
                n.cmd("--neighbours", 3)
            client = new_mock_client(a.mock_host, a.mock_port, a.source_call)
            try:
                client.send_keep()
                wait(2)
                base = 0x4D300000 | (int(time.time()) & 0xFFF) << 4
                for i in range(a.frames):
                    mid = base + i
                    ids.append(mid)
                    client.send_data(
                        gwrx_frame.build(
                            mid, plan.flags, a.source_call, a.group, f"GWRX01 bench {i + 1}"
                        )
                    )
                    print(f"frame {i + 1}/{a.frames} id {mid:08X}")
                    if i + 1 < a.frames:
                        wait(a.interval)
                wait(TAIL_WAIT_S)
            finally:
                client.close()
        completed = True
    except SetupError as e:
        print(f"setup failed: {e}")
        rc = 2
    except RunAborted as e:
        print(f"run aborted: {e}")
        rc = 4
    except KeyboardInterrupt:
        print("interrupted: restoring")
        rc = 130
    except Exception as e:
        print(f"run failed: {e!r}")
        rc = 2
    finally:
        with sigint_ignored():
            try:
                restored = restore_nodes(plan, nodes, began, state)
            except Exception as e:
                print(f"restore crashed: {e!r}", file=sys.stderr)
                restored = False
            for n in nodes:
                try:
                    n.close()
                except Exception as e:
                    print(f"{n.call}: close failed: {e!r}")
    if not restored:
        rc = 3  # a node left in bench state outranks every other outcome
    if not completed:
        return rc
    text = "\n".join(format_summary(n.call, tally(n.lines, ids)) for n in nodes)
    print(text)
    summ = a.log_dir / f"gwrx-{a.tag}-{stamp}.summary"
    summ.write_text(text + "\n")
    print(f"summary: {summ}")
    return rc


def main(argv: Optional[list[str]] = None) -> int:
    ap = build_parser()
    args = ap.parse_args(argv)
    try:
        plan = Plan(args)
    except PlanError as e:
        ap.error(str(e))
    print(plan.describe())
    args.log_dir.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y%m%d-%H%M%S")
    nodes = [
        Node(
            call,
            make_transport(kind, value, call),
            args.log_dir / f"gwrx-{args.tag}-{call.lower()}-{stamp}.log",
        )
        for call, kind, value in plan.nodes
    ]
    try:
        return run(plan, nodes, stamp)
    except PlanError as e:
        print(f"error: {e}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:  # only before run() owned the nodes
        print("interrupted", file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main())
