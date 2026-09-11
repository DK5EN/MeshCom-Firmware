#!/usr/bin/env python3
"""Console command golden for the MeshCom bench (test plan D2-V / step H3).

Drives every command of `test/golden/corpus/commands/script.txt` at a node and
records the answer per command, over **both** transports -- USB serial and the
TCP 2323 net console -- because they are different code paths into the same
dispatcher (`checkSerialCommand()` has one copy per platform, audit `D1-03`).

This is the safety net the command-table rewrite rests on. `D2-06`/`D2-07`
replace a 5,900-line if/else ladder with a table; nothing but a before/after
capture of every branch can show that the 309 commands still answer the same.

    python3 tools/bench/console_golden.py --port /dev/cu.usbserial-0001 \\
        --script test/golden/corpus/commands/script.txt --out G0/heltec-93/
    python3 tools/bench/console_golden.py --host 192.168.68.69 --password '' ...
    python3 tools/bench/console_golden.py --compare a/cmd-usb.txt b/cmd-usb.txt
    python3 tools/bench/console_golden.py --self-test

**Restore the node before and after.** The script drives `--<cmd> 1 / 999999 /
abc` for every setter in the ladder; a run reconfigures the node thoroughly.
`test/golden/backup_nodes.py --restore` is the containment.

Output, per transport:

    cmd-<transport>.txt   one block per command: the command, then its answer
    cmd-<transport>-async.txt   lines attributed to no command (see below)

Async traffic is the hard part, exactly as in the BLE capture. A bench node
talks on its own -- GPS fixes, the heap monitor, received LoRa frames, the
EXTUDP echo -- and those lines land in the middle of a command's answer. They
are routed to the async file by prefix, and the prefix list is explicit and
printed in the header of every capture, so a reader can see what was filtered
rather than guessing.

No third-party dependencies beyond pyserial (already a PlatformIO dependency).
"""

from __future__ import annotations

import argparse
import hashlib
import hmac
import re
import signal
import socket
import sys
import time
from pathlib import Path
from typing import Iterable, List, Optional, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "test" / "golden"))

# Line prefixes the node emits without being asked. Kept explicit rather than
# clever: every one of these was seen interleaving a command answer during the
# UDP-1990 captures of 2026-09-11.
ASYNC_PREFIXES = (
    "[GPS ", "[HEAP", "[EXT", "[LoRa]", "[MC-DBG]", "[WIFI]", "[ETH]",
    "[GW];", "[UDP];", "[INSTR", "[POSINFO]", "[readBatteryVoltage]",
    "[NTP];", "[BOOT]", "[TX];", "[RX];", "[SRVIP];",
)


def is_async(line: str) -> bool:
    stripped = line.lstrip()
    return any(stripped.startswith(p) for p in ASYNC_PREFIXES)


class Transport:
    """Minimal common shape over the serial port and the 2323 socket."""

    def send(self, text: str) -> None: ...
    def read(self, timeout: float) -> bytes: ...
    def close(self) -> None: ...


class SerialTransport(Transport):
    def __init__(self, port: str, dtr: bool) -> None:
        import serial

        self._serial = serial
        self.port = port
        self.dtr = dtr
        self._open()

    def _open(self) -> None:
        s = self._serial.Serial()
        s.port = self.port
        s.baudrate = 115200
        s.timeout = 0.1
        s.dtr = self.dtr
        s.rts = False
        s.open()
        self.s = s

    def send(self, text: str) -> None:
        self.s.write(text.encode() + b"\r")
        self.s.flush()

    def read(self, timeout: float) -> bytes:
        # The RAK4631's CDC port disappears mid-run and comes back; without
        # reopening, a capture is lost at whatever point the host hiccuped.
        try:
            return self.s.read(4096)
        except Exception:
            try:
                self.s.close()
            except Exception:
                pass
            time.sleep(1.0)
            for _ in range(15):
                try:
                    self._open()
                    return b""
                except Exception:
                    time.sleep(1.0)
            raise

    def close(self) -> None:
        try:
            self.s.close()
        except Exception:
            pass


class TcpTransport(Transport):
    """The HMAC console of `net_console.cpp`, protocol per tools/hmac_connect.py.

    Reconnects on a reset. The node drops the console connection during a long
    run -- seen mid-capture on Heltec-93, `ConnectionResetError` -- and without
    reconnecting the rest of the script goes nowhere. The console is
    single-client, so a reconnect is only safe because nothing else is using it.
    """

    def __init__(self, host: str, port: int, password: str) -> None:
        self.host, self.port, self.password = host, port, password
        self.resets = 0
        self._connect()

    def _connect(self) -> None:
        host, port, password = self.host, self.port, self.password
        self.sock = socket.create_connection((host, port), timeout=10)
        self.sock.settimeout(0.2)
        first = b""
        deadline = time.time() + 5
        while b"\n" not in first and time.time() < deadline:
            try:
                first += self.sock.recv(4096)
            except socket.timeout:
                pass
        line = first.split(b"\n", 1)[0].decode(errors="replace").strip()
        if password:
            if not line.upper().startswith("NONCE:"):
                raise RuntimeError(f"expected a NONCE challenge, got {line!r}")
            nonce = bytes.fromhex(line.split()[1].strip())
            digest = hmac.new(password.encode(), nonce, hashlib.sha256).hexdigest()
            self.sock.sendall((digest + "\n").encode())

    def _reconnect(self) -> None:
        """Reconnect, but give up fast once the console is simply gone.

        `--netconsole off` disables the very port this transport uses. The
        first version retried for 20 s per command after that, which burned a
        28-minute run into nothing. A refused connection is not a transient
        drop: three failures in a row means the console is down, and the run
        should end with its partial capture written rather than grind on.
        """
        self.resets += 1
        try:
            self.sock.close()
        except Exception:
            pass
        for attempt in range(3):
            try:
                self._connect()
                return
            except Exception:
                time.sleep(1.0)
        raise KeyboardInterrupt(
            "the 2323 console stopped accepting connections -- was it turned "
            "off by a command in the script?")

    def send(self, text: str) -> None:
        try:
            self.sock.sendall((text + "\n").encode())
        except Exception:
            self._reconnect()
            try:
                self.sock.sendall((text + "\n").encode())
            except Exception:
                pass

    def read(self, timeout: float) -> bytes:
        try:
            return self.sock.recv(4096)
        except socket.timeout:
            return b""
        except Exception:
            self._reconnect()
            return b""

    def close(self) -> None:
        try:
            self.sock.close()
        except Exception:
            pass


def read_script(path: Path) -> List[str]:
    return [
        ln.strip()
        for ln in path.read_text().splitlines()
        if ln.strip() and not ln.strip().startswith("#")
    ]


def drive(transport: Transport, commands: List[str], *,
          quiet: float, cap: float, settle_min: float = 4.0,
          settle_quiet: float = 2.0,
          settle_max: float = 40.0,
          out_answers: Optional[List[Tuple[str, List[str]]]] = None,
          out_async: Optional[List[str]] = None,
          progress: int = 25) -> Tuple[List[Tuple[str, List[str]]], List[str]]:
    """Send each command, collect its answer, split async lines out.

    A command's answer ends when the node has been silent for `quiet` seconds,
    or after `cap` seconds -- whichever comes first. The cap matters: a handful
    of commands (`--mheard` on a busy node) answer for a long time, and without
    it one command can swallow the rest of the run.
    """
    answers = out_answers if out_answers is not None else []
    async_lines = out_async if out_async is not None else []
    buf = b""

    def pump(seconds: float) -> None:
        nonlocal buf
        end = time.time() + seconds
        while time.time() < end:
            chunk = transport.read(0.1)
            if chunk:
                buf += chunk

    # Opening the port resets every ESP32 board on this bench, so the first
    # commands would otherwise be typed into a booting node: measured on
    # Heltec-93, they came back as "...wrong command --compress 1" with the
    # leading character eaten. Wait for the boot flood to stop before the first
    # command, the same settle the BLE capture needs for the same reason.
    settle_start = time.time()
    last_len = -1
    quiet_since = time.time()
    while time.time() - settle_start < settle_max:
        pump(0.2)
        if len(buf) != last_len:
            last_len = len(buf)
            quiet_since = time.time()
        elif (time.time() - quiet_since >= settle_quiet
              and time.time() - settle_start >= settle_min):
            break
    buf = b""

    for command in commands:
        transport.send(command)
        last_len = -1
        start = time.time()
        quiet_since = time.time()
        while time.time() - start < cap:
            pump(0.1)
            if len(buf) != last_len:
                last_len = len(buf)
                quiet_since = time.time()
            elif time.time() - quiet_since >= quiet:
                break
        text = buf.decode("utf-8", errors="replace")
        buf = b""
        lines = [ln.rstrip("\r") for ln in text.split("\n") if ln.strip()]
        own = [ln for ln in lines if not is_async(ln)]
        async_lines += [ln for ln in lines if is_async(ln)]
        answers.append((command, own))
        if progress and len(answers) % progress == 0:
            print(f"  {len(answers)}/{len(commands)} commands", file=sys.stderr)

    return answers, async_lines


def render(answers: List[Tuple[str, List[str]]], transport_name: str,
           complete: bool = True) -> str:
    import normalize

    n = normalize.Normalizer()
    out = [
        f"# console golden, transport: {transport_name}",
        f"# generated by tools/bench/console_golden.py",
        f"# COMPLETE: {'yes' if complete else 'NO -- partial capture, do not compare'}",
        f"# async line prefixes filtered into the -async file: "
        + " ".join(ASYNC_PREFIXES),
        "",
    ]
    for command, lines in answers:
        out.append(f">>> {command}")
        out.extend("    " + n.line(ln) for ln in lines)
        out.append("")
    return "\n".join(out) + "\n"


def compare(a: Path, b: Path) -> int:
    """Block-by-block diff of two captures, keyed on the command."""
    def blocks(p: Path):
        current, out = None, {}
        order = []
        for line in p.read_text().splitlines():
            if line.startswith(">>> "):
                current = line[4:]
                out[current] = []
                order.append(current)
            elif current is not None and line.startswith("    "):
                out[current].append(line[4:])
        return order, out

    order_a, ba = blocks(a)
    order_b, bb = blocks(b)
    only_a = [c for c in order_a if c not in bb]
    only_b = [c for c in order_b if c not in ba]
    changed = [c for c in order_a if c in bb and ba[c] != bb[c]]

    print(f"{a}: {len(order_a)} commands\n{b}: {len(order_b)} commands")
    for c in only_a:
        print(f"  only in a: {c}")
    for c in only_b:
        print(f"  only in b: {c}")
    for c in changed:
        print(f"  differs: {c}")
        for line in ba[c][:4]:
            print(f"      a: {line}")
        for line in bb[c][:4]:
            print(f"      b: {line}")
    bad = len(only_a) + len(only_b) + len(changed)
    print("identical" if not bad else f"{bad} difference(s)")
    return 1 if bad else 0


def _self_test() -> int:
    failures = 0
    for line in ("[GPS ]...fix:yes", "  [HEAP] 00:00:00 1 2 3", "[EXT] Out: {}"):
        if not is_async(line):
            failures += 1
            print(f"FAIL: {line!r} should be async")
    for line in ("--info", "...CALL: DK5EN-90", "[MAXHOP];text;4;pos;2"):
        if is_async(line):
            failures += 1
            print(f"FAIL: {line!r} should not be async")

    text = render([("--maxhop", ["[MAXHOP];text;4;pos;2"]),
                   ("--info", ["...CALL: DK5EN-90"])], "test")
    if ">>> --maxhop" not in text or "    [MAXHOP];text;4;pos;2" not in text:
        failures += 1
        print("FAIL: render block shape")

    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        a = Path(tmp) / "a.txt"
        b = Path(tmp) / "b.txt"
        a.write_text(text)
        b.write_text(text)
        if compare(a, b) != 0:
            failures += 1
            print("FAIL: a capture must equal itself")
        b.write_text(text.replace("text;4", "text;5"))
        if compare(a, b) == 0:
            failures += 1
            print("FAIL: a changed answer must be reported")

    print("console_golden.py self-test: "
          + ("ok" if failures == 0 else f"{failures} failure(s)"))
    return 1 if failures else 0


def main(argv: Iterable[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--port", help="serial device, e.g. /dev/cu.usbserial-0001")
    ap.add_argument("--dtr", action="store_true", help="assert DTR (RAK4631 needs it)")
    ap.add_argument("--host", help="node IP for the TCP 2323 net console")
    ap.add_argument("--tcp-port", type=int, default=2323)
    ap.add_argument("--password", default="", help="net-console HMAC password")
    ap.add_argument("--script", type=Path,
                    default=Path("test/golden/corpus/commands/script.txt"))
    ap.add_argument("--out", type=Path, help="capture directory")
    ap.add_argument("--quiet", type=float, default=0.35,
                    help="silence that ends a command's answer")
    ap.add_argument("--cap", type=float, default=4.0,
                    help="hard limit on one command's answer")
    ap.add_argument("--settle-min", type=float, default=4.0,
                    help="minimum wait after opening, before the first command; "
                         "opening the port resets every ESP32 board here")
    ap.add_argument("--settle-quiet", type=float, default=2.0,
                    help="silence that ends the boot flood")
    ap.add_argument("--limit", type=int, default=0, help="only the first N commands")
    ap.add_argument("--compare", nargs=2, type=Path, metavar="TXT")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args(list(argv) if argv is not None else None)

    if args.self_test:
        return _self_test()
    if args.compare:
        return compare(*args.compare)
    if not args.out:
        ap.error("--out is required for a capture")

    commands = read_script(args.script)
    if args.limit:
        commands = commands[:args.limit]

    if args.port:
        transport, name = SerialTransport(args.port, args.dtr), "usb"
    elif args.host:
        transport, name = TcpTransport(args.host, args.tcp_port, args.password), "2323"
    else:
        ap.error("pass --port or --host")

    print(f"driving {len(commands)} commands over {name}", file=sys.stderr)

    # A 435-command run over TCP takes tens of minutes and the first one was
    # killed by its `timeout` wrapper with nothing written: the capture only
    # wrote its files at the very end, so half an hour of driving a node was
    # lost silently. Now the partial result is written whatever happens --
    # killed, crashed, or finished -- and the header says how far it got.
    answers: List[Tuple[str, List[str]]] = []
    async_lines: List[str] = []
    interrupted = False

    def on_term(_signum, _frame):
        raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, on_term)

    try:
        drive(transport, commands, quiet=args.quiet, cap=args.cap,
              settle_min=args.settle_min, settle_quiet=args.settle_quiet,
              out_answers=answers, out_async=async_lines)
    except KeyboardInterrupt:
        interrupted = True
        print(f"\ninterrupted after {len(answers)} of {len(commands)} commands; "
              f"writing the partial capture", file=sys.stderr)
    finally:
        transport.close()

    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / f"cmd-{name}.txt").write_text(
        render(answers, name, complete=len(answers) == len(commands)))
    (args.out / f"cmd-{name}-async.txt").write_text("\n".join(async_lines) + "\n")
    answered = sum(1 for _c, lines in answers if lines)
    resets = getattr(transport, "resets", 0)
    print(f"{len(answers)} commands, {answered} answered, "
          f"{len(async_lines)} async lines filtered"
          + (f", {resets} transport reset(s) recovered" if resets else ""),
          file=sys.stderr)
    return 1 if interrupted else 0


if __name__ == "__main__":
    raise SystemExit(main())
