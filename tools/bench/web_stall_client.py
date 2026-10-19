#!/usr/bin/env python3
"""Provoke and detect a TASK_WDT reboot from a web client that does not read.

Why: the ESP32 web server runs synchronously in the main loop and answers every
request HTTP/1.x `Connection: close`. A page is built row by row with write()
calls; if the client never drains its socket the TCP window fills, write()
blocks, the loop is stuck for more than 5 s and TASK_WDT reboots the node.

URL: web_functions.cpp (around line 1001-1160) matches `web_header.indexOf(
"/?page=<name>")` and answers with send_http_header(200) + sub_page_<name>().
So `GET /?page=neighbours` is NOT a scaffold: it is the real fragment (the big
matrix table) that the scaffold's loadPage() XHR fetches. Only `GET /` returns
the scaffold. We therefore request `/?page=<name>` directly. With a web password
set the node answers the login fragment instead (small page: no stall possible).

Modes
  --stall    tiny SO_RCVBUF (set before connect), send the request, never read
             for --hold seconds (default 20)
  --trickle  read --rate bytes/s (default 200)
  --normal   read everything, report bytes and duration

Detection (console TCP 2323, ESP32 only; --no-console to skip)
  Login as tools/bench/identity_guard.py read_info_net: "NONCE: <hex>" ->
  HMAC-SHA256(password, nonce); password from env MC_CONSOLE_PW only.
  A background thread records the console for the whole run. Reboot evidence:
    - boot lines: "[BOOT] RESET_REASON=<n> <NAME>" (esp32_main.cpp), "[BOOT] LAST_LOOP_SECTION=",
      "[BOOT];ready", ROM "rst:0x", "Task watchdog"
    - the console dropped (peer closed/reset) while we did not close it. The
      console connection dies on a reboot and the boot banner is printed before
      we can reconnect, so a drop alone counts as "rebooted" (evidence lists it).
  Firmware fix log lines are collected: "[WEB];tx;abort;<stall|budget|error>;
  sent;<n>;ms;<n>" and "[WEB];tx;done;sent;<n>;ms;<n>;flushes;<n>".
  A dropped console is only secondary evidence (after an ESP32 reboot the old
  connection is often never reset), so it does not decide the verdict.
  Positive reboot detection: `--info` over a FRESH console connection (reuses
  identity_guard.read_info_net) BEFORE the request and AFTER the settle phase
  (retried up to --probe-timeout). It carries "TIME <ms> ms" (uptime) and
  "BOOT RESET_REASON=<n> <NAME> LAST_LOOP_SECTION=<sec>". Rule: rebooted when
  after_uptime < before_uptime + elapsed - 5000 ms (uptime went backwards).
  A failed HTTP probe with a valid non-reset --info = survived (a pf/dummynet
  delay on port 80 can fail the probe, see web_throttle.sh). With --no-console
  only the HTTP liveness probe is used; no answer at all -> exit 2.

Output: `VERDICT;<rebooted|survived>;abort=<reason|none>;bytes=<n>;ms=<n>` and
`UPTIME;before=<ms>;after=<ms>;reset=<NAME or ->`, optional --json summary. Exit 0 survived, 1 rebooted, 2 tool/network error.
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
import threading
import time
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))

from identity_guard import read_info_net  # noqa: E402  (does the HMAC login)

EXIT_SURVIVED, EXIT_REBOOTED, EXIT_ERROR = 0, 1, 2
UPTIME_TOLERANCE_MS = 5000

PAGES = {"neighbours", "path", "mheard"}

BOOT_RE = re.compile(
    r"\[BOOT\][; ]RESET_REASON=|\[BOOT\][; ]LAST_LOOP_SECTION=|\[BOOT\];ready|\[BOOT\] ready|"
    r"\brst:0x[0-9a-fA-F]+|Task watchdog|TASK_WDT"
)
TIME_RE = re.compile(r"TIME\s+(\d+)\s*ms")
INFO_BOOT_RE = re.compile(
    r"BOOT\s+RESET_REASON=(\d+)\s+(\S+)(?:\s+LAST_LOOP_SECTION=(\S+))?(?:\s+entered_ms=(\d+))?"
)
WEB_RE = re.compile(r"\[WEB\];tx;(abort|done);(.*)$")


class StallError(Exception):
    """Tool or network problem (exit 2)."""


def page_path(page: str) -> str:
    """Map --page to a request path: a known name -> /?page=<name>, a raw path as is."""
    if page.startswith("/"):
        return page
    return f"/?page={page}"


def build_request(path: str, host: str) -> bytes:
    return f"GET {path} HTTP/1.1\r\nHost: {host}\r\nConnection: close\r\n\r\n".encode()


def is_boot_line(line: str) -> bool:
    return BOOT_RE.search(line) is not None


def parse_web_line(line: str) -> dict[str, Any] | None:
    """Parse '[WEB];tx;abort;<reason>;sent;<n>;ms;<n>' or '[WEB];tx;done;sent;<n>;ms;<n>;flushes;<n>'."""
    m = WEB_RE.search(line)
    if not m:
        return None
    kind, rest = m.group(1), m.group(2).split(";")
    out: dict[str, Any] = {"kind": kind}
    if kind == "abort":
        if not rest or not rest[0]:
            return None
        out["reason"] = rest[0]
        rest = rest[1:]
    if len(rest) % 2:
        return None
    for k, v in zip(rest[0::2], rest[1::2]):
        if not v.lstrip("-").isdigit():
            return None
        out[k] = int(v)
    return out


def parse_info(text: str) -> dict[str, Any]:
    """Extract uptime_ms and the boot summary from --info text (last match wins)."""
    out: dict[str, Any] = {"uptime_ms": None, "reset_reason": None, "reset_name": None,
                           "last_loop_section": None, "entered_ms": None}
    for m in TIME_RE.finditer(text):
        out["uptime_ms"] = int(m.group(1))
    for m in INFO_BOOT_RE.finditer(text):
        out["reset_reason"] = int(m.group(1))
        out["reset_name"] = m.group(2)
        out["last_loop_section"] = m.group(3)
        out["entered_ms"] = int(m.group(4)) if m.group(4) else None
    return out


def uptime_rebooted(before_ms: int, after_ms: int, elapsed_ms: int, tolerance: int = UPTIME_TOLERANCE_MS) -> bool:
    """True when the uptime went backwards relative to the wall clock."""
    return after_ms < before_ms + elapsed_ms - tolerance


def fetch_info(host: str, password: str, port: int, timeout: float = 0.0) -> dict[str, Any] | None:
    """Read --info over a fresh console connection; retry for `timeout` seconds. None if unreadable."""
    deadline = time.monotonic() + timeout
    while True:
        try:
            info = parse_info(read_info_net(host, password=password, port=port))
            if info["uptime_ms"] is not None:
                return info
        except (OSError, Exception):  # noqa: BLE001 - node down / login refused: retry
            pass
        if time.monotonic() >= deadline:
            return None
        time.sleep(1.0)


def make_verdict(rebooted: bool, abort: str | None, nbytes: int, ms: int) -> str:
    return f"VERDICT;{'rebooted' if rebooted else 'survived'};abort={abort or 'none'};bytes={nbytes};ms={ms}"


class Console:
    """Net console (2323) capture in a background thread."""

    def __init__(self, host: str, password: str = "", port: int = 2323) -> None:
        self.host, self.password, self.port = host, password, port
        self.lines: list[str] = []
        self.dropped = False
        self._stop = threading.Event()
        self._sock: socket.socket | None = None
        self._thread: threading.Thread | None = None
        self._lock = threading.Lock()

    def connect(self) -> None:
        s = socket.create_connection((self.host, self.port), timeout=10)
        s.settimeout(0.3)
        out = b""
        end = time.time() + 1.5
        while time.time() < end:
            try:
                d = s.recv(4096)
                if not d:
                    break
                out += d
            except socket.timeout:
                pass
            if out.startswith(b"NONCE:") and b"\n" in out:
                break
        pending = b""
        if out.startswith(b"NONCE:"):
            if not self.password:
                s.close()
                raise StallError("console wants a password (set MC_CONSOLE_PW)")
            nonce = bytes.fromhex(out.split(b"\n", 1)[0].split()[1].decode())
            s.sendall(hmac.new(self.password.encode(), nonce, hashlib.sha256).hexdigest().encode() + b"\n")
            s.settimeout(3)
            reply = s.recv(64)
            if not reply.startswith(b"OK"):
                s.close()
                raise StallError(f"console login refused ({reply.strip()!r})")
            s.settimeout(0.3)
            pending = reply.split(b"\n", 1)[1] if b"\n" in reply else b""
        else:
            pending = out
        self._sock = s
        self._thread = threading.Thread(target=self._rx, args=(pending,), daemon=True)
        self._thread.start()

    def _rx(self, pending: bytes) -> None:
        assert self._sock is not None
        while not self._stop.is_set():
            while b"\n" in pending:
                line, pending = pending.split(b"\n", 1)
                with self._lock:
                    self.lines.append(line.decode("utf-8", "replace").rstrip("\r"))
            try:
                d = self._sock.recv(4096)
            except socket.timeout:
                continue
            except OSError:
                d = b""
            if not d:
                if not self._stop.is_set():
                    self.dropped = True
                return
            pending += d

    def snapshot(self) -> list[str]:
        with self._lock:
            return list(self.lines)

    def close(self) -> None:
        self._stop.set()
        if self._sock:
            try:
                self._sock.close()
            except OSError:
                pass
        if self._thread:
            self._thread.join(2)


def run_request(
    host: str, port: int, path: str, mode: str, hold: float, rate: int, rcvbuf: int, timeout: float
) -> tuple[int, int, str | None]:
    """Return (bytes_read, ms, error). --stall reads nothing."""
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    if mode == "stall":
        s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, rcvbuf)  # before connect: small window
    s.settimeout(timeout)
    t0 = time.monotonic()
    nbytes = 0
    err: str | None = None
    try:
        s.connect((host, port))
        s.sendall(build_request(path, host))
        if mode == "stall":
            time.sleep(hold)
        elif mode == "trickle":
            while True:
                d = s.recv(max(1, rate))
                if not d:
                    break
                nbytes += len(d)
                time.sleep(1.0)
        else:
            while True:
                d = s.recv(65536)
                if not d:
                    break
                nbytes += len(d)
    except (OSError, socket.timeout) as e:
        err = f"{type(e).__name__}: {e}"
    finally:
        s.close()
    return nbytes, int((time.monotonic() - t0) * 1000), err


def probe_http(host: str, port: int, timeout: float) -> bool:
    """Liveness: GET / must be answered with an HTTP status line within timeout."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            with socket.create_connection((host, port), timeout=min(3.0, timeout)) as s:
                s.settimeout(min(3.0, timeout))
                s.sendall(build_request("/", host))
                if s.recv(64).startswith(b"HTTP/"):
                    return True
        except OSError:
            pass
        time.sleep(0.5)
    return False


def summarize(lines: list[str]) -> dict[str, Any]:
    boot = [ln for ln in lines if is_boot_line(ln)]
    web = [w for w in (parse_web_line(ln) for ln in lines) if w]
    abort = next((w["reason"] for w in web if w["kind"] == "abort"), None)
    return {"boot_lines": boot, "web_lines": web, "abort": abort}


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("host")
    ap.add_argument("--page", default="neighbours", help="neighbours|path|mheard|raw path starting with /")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--stall", dest="mode", action="store_const", const="stall")
    g.add_argument("--trickle", dest="mode", action="store_const", const="trickle")
    g.add_argument("--normal", dest="mode", action="store_const", const="normal")
    ap.set_defaults(mode="stall")
    ap.add_argument("--hold", type=float, default=20.0, help="--stall: seconds without reading")
    ap.add_argument("--rate", type=int, default=200, help="--trickle: bytes per second")
    ap.add_argument("--rcvbuf", type=int, default=1024, help="--stall: SO_RCVBUF before connect")
    ap.add_argument("--http-port", type=int, default=80)
    ap.add_argument("--console-port", type=int, default=2323)
    ap.add_argument("--no-console", action="store_true", help="HTTP liveness only (RAK4631)")
    ap.add_argument("--probe-timeout", type=float, default=30.0)
    ap.add_argument("--timeout", type=float, default=30.0, help="socket timeout of the request")
    ap.add_argument("--settle", type=float, default=8.0, help="seconds to keep the console after the request")
    ap.add_argument("--json", type=Path, help="write a summary here")
    return ap.parse_args(argv)


def run(args: argparse.Namespace) -> int:
    console: Console | None = None
    password = os.environ.get("MC_CONSOLE_PW", "")
    before: dict[str, Any] | None = None
    t_before = 0.0
    if not args.no_console:
        before = fetch_info(args.host, password, args.console_port)
        t_before = time.monotonic()
        if before is None:
            print("ERROR;console;cannot read --info before the request", file=sys.stderr)
            return EXIT_ERROR
        console = Console(args.host, password, args.console_port)
        try:
            console.connect()
        except (OSError, StallError) as e:
            print(f"ERROR;console;{e}", file=sys.stderr)
            return EXIT_ERROR
    path = page_path(args.page)
    nbytes, ms, req_err = run_request(
        args.host, args.http_port, path, args.mode, args.hold, args.rate, args.rcvbuf, args.timeout
    )
    time.sleep(args.settle if console else 0)
    after: dict[str, Any] | None = None
    elapsed_ms = 0
    if console:
        after = fetch_info(args.host, password, args.console_port, args.probe_timeout)
        elapsed_ms = int((time.monotonic() - t_before) * 1000)
    alive = probe_http(args.host, args.http_port, args.probe_timeout if after is None else 3.0)
    lines: list[str] = []
    dropped = False
    if console:
        lines = console.snapshot()
        dropped = console.dropped
        console.close()
    info = summarize(lines)
    evidence = list(info["boot_lines"])
    uptime_reset = False
    if before and after:
        uptime_reset = uptime_rebooted(before["uptime_ms"], after["uptime_ms"], elapsed_ms)
        if uptime_reset:
            evidence.append("uptime_reset")
            if after["reset_name"]:
                evidence.append(f"reset_reason={after['reset_name']}")
            if after["last_loop_section"]:
                evidence.append(f"last_loop_section={after['last_loop_section']}")
    # Boot lines on the live console are positive; with a readable --info it is authoritative.
    rebooted = uptime_reset if (before and after) else bool(evidence)
    done = next((w for w in info["web_lines"] if w["kind"] == "done"), None)
    if done and not nbytes:
        nbytes = done.get("sent", 0)
    reset_name = after["reset_name"] if (after and uptime_reset) else None
    summary = {
        "host": args.host, "mode": args.mode, "path": path, "rebooted": rebooted,
        "abort": info["abort"], "bytes": nbytes, "ms": ms, "http_alive": alive,
        "request_error": req_err, "evidence": evidence, "web_lines": info["web_lines"],
        "console_dropped": dropped,
        "uptime_before_ms": before["uptime_ms"] if before else None,
        "uptime_after_ms": after["uptime_ms"] if after else None,
        "elapsed_ms": elapsed_ms,
        "uptime_reset": uptime_reset,
        "after_boot": ({k: after[k] for k in ("reset_reason", "reset_name", "last_loop_section", "entered_ms")}
                       if after else None),
    }
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(summary, indent=2) + "\n")
    print(make_verdict(rebooted, info["abort"], nbytes, ms))
    if console:
        print(f"UPTIME;before={summary['uptime_before_ms']};after={summary['uptime_after_ms']};reset={reset_name or '-'}")
    if console and after is None and not rebooted:
        print("ERROR;--info unreadable after the run and no boot evidence", file=sys.stderr)
        return EXIT_ERROR
    if not console and not alive:
        print("ERROR;node does not answer HTTP", file=sys.stderr)
        return EXIT_ERROR
    return EXIT_REBOOTED if rebooted else EXIT_SURVIVED


def main(argv: list[str] | None = None) -> int:
    try:
        return run(parse_args(argv))
    except (OSError, StallError) as e:
        print(f"ERROR;{e}", file=sys.stderr)
        return EXIT_ERROR


if __name__ == "__main__":
    sys.exit(main())
