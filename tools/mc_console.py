#!/usr/bin/env python3
"""Shared client for the node's HMAC net console (TCP 2323).

One login implementation for every host tool that talks to the console
(``src/net_console.cpp``)::

    <- "NONCE: <32 hex>\\r\\n"                      (node has a password)
    -> "<64 hex HMAC-SHA256(password, nonce)>\\n"
    <- "OK\\r\\n<banner>"  or  "FAIL\\r\\n" + close
    <- "OK\\r\\n<banner>"                           (node has no password)

The console is single-client: a new authenticated login replaces the running
session, so two tools on one node kick each other off.

**Passwords never go on a command line.** They are looked up per callsign:

1. ``MC_CONSOLE_PW`` in the environment -- explicit override for one run
   (set but empty forces "no password").
2. ``MC_PW_<CALL>`` (``DK5EN-92`` -> ``MC_PW_DK5EN_92``) in the environment,
   then in the repo's gitignored ``.env``. Outside a git checkout (the
   rpizero copy in ~/meshlog) it is the ``.env`` next to this file;
   ``MC_ENV_FILE`` points anywhere else.

The callsign comes from the caller (``--call``, a fleet.json entry) or from a
host name like ``dk5en-14.local``. Without one, a node that asks for a
password is a clear error, never a blind HMAC attempt. ``.env`` must be mode
600 (refused otherwise, like ssh does with a private key).

Stdlib only: it is deployed next to ``meshlogger.py`` on the rpizero.
"""

from __future__ import annotations

import hashlib
import hmac
import os
import re
import socket
import stat
import time
from pathlib import Path
from typing import Dict, Optional

PORT = 2323
OVERRIDE_VAR = "MC_CONSOLE_PW"
ENV_FILE_VAR = "MC_ENV_FILE"
KEY_PREFIX = "MC_PW_"
# Callsign with SSID, as fleet.json and the firmware use it (DK5EN-92).
CALL_RE = re.compile(r"^[A-Z0-9]{3,7}-\d{1,2}$")
# The firmware keeps node_passwd as "%-14.14s" and strips the padding
# (netConsoleSetPassword), so the HMAC key is at most 14 chars, no trailing blanks.
FW_PW_MAX = 14


class LoginError(ConnectionError):
    """The console refused the login (FAIL, or it wants a password we do not have).

    Retrying cannot help. A connection that drops or stays silent during the
    login raises a plain ConnectionError instead: that one is transient (node
    rebooting, another login in progress)."""


class EnvFileError(RuntimeError):
    """The .env file exists but must not be used (permissions, syntax)."""


def env_file() -> Path:
    """``MC_ENV_FILE`` if set, else ``.env`` in the git checkout holding this
    file, else ``.env`` next to this file (a standalone deploy)."""
    explicit = os.environ.get(ENV_FILE_VAR)
    if explicit:
        return Path(explicit).expanduser()
    here = Path(__file__).resolve()
    for d in here.parents:
        if (d / ".git").exists():
            return d / ".env"
    return here.parent / ".env"


def load_env(path: Optional[Path] = None) -> Dict[str, str]:
    """Parse KEY=VALUE lines (``#`` comments, optional quotes). Missing file = {}."""
    path = path if path is not None else env_file()
    if not path.exists():
        return {}
    mode = path.stat().st_mode
    if mode & (stat.S_IRWXG | stat.S_IRWXO):
        raise EnvFileError("%s is readable by others (mode %o); run: chmod 600 %s"
                           % (path, stat.S_IMODE(mode), path))
    out: Dict[str, str] = {}
    for n, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("export "):
            line = line[len("export "):].lstrip()
        key, sep, val = line.partition("=")
        key = key.strip()
        if not sep or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", key):
            # The line number only: the line itself may hold a secret.
            raise EnvFileError("%s:%d: not a KEY=VALUE line" % (path, n))
        val = val.strip()
        if len(val) >= 2 and val[0] == val[-1] and val[0] in "'\"":
            val = val[1:-1]
        out[key] = val
    return out


def pw_key(call: str) -> str:
    """``DK5EN-92`` -> ``MC_PW_DK5EN_92``."""
    return KEY_PREFIX + call.upper().replace("-", "_")


def call_from_host(host: str) -> Optional[str]:
    """``dk5en-14.local`` / ``DK5EN-14`` -> ``DK5EN-14``; an IP address -> None."""
    label = host.split(".", 1)[0].upper()
    return label if CALL_RE.match(label) else None


def password_for(call: Optional[str] = None, host: Optional[str] = None) -> str:
    """The console password for `call` (or the call in `host`); "" if none is known."""
    if OVERRIDE_VAR in os.environ:
        return os.environ[OVERRIDE_VAR]
    call = call or (call_from_host(host) if host else None)
    if not call:
        return ""
    key = pw_key(call)
    if key in os.environ:
        return os.environ[key]
    return load_env().get(key, "")


def _fw_key(password: str) -> bytes:
    return password.rstrip(" ")[:FW_PW_MAX].encode()


def _read_line(sock: socket.socket, buf: bytes, deadline: float, what: str) -> tuple:
    while b"\n" not in buf:
        if time.monotonic() > deadline:
            raise ConnectionError("no %s within the timeout (another login in progress?)" % what)
        try:
            chunk = sock.recv(256)
        except socket.timeout:
            continue
        if not chunk:
            raise ConnectionError("connection closed before the %s" % what)
        buf += chunk
    line, _, rest = buf.partition(b"\n")
    return line.strip(), rest


def login(sock: socket.socket, password: str, timeout: float = 5.0, label: str = "") -> bytes:
    """Run the challenge/response on a fresh connection.

    Returns the bytes received after the greeting (banner, first log lines) so
    the caller loses nothing. `label` names the missing setting in errors.
    The socket must have a timeout set (any value; the deadline rules).
    """
    deadline = time.monotonic() + timeout
    first, rest = _read_line(sock, b"", deadline, "greeting")
    if first.startswith(b"NONCE:"):
        if not password:
            if label == OVERRIDE_VAR:
                raise LoginError("the console wants a password, but %s is set and empty" % OVERRIDE_VAR)
            raise LoginError("the console wants a password; set %s in .env" % (label or "MC_PW_<CALL>"))
        try:
            nonce = bytes.fromhex(first.split(b":", 1)[1].strip().decode())
        except ValueError:
            raise LoginError("malformed challenge %r" % first[:48]) from None
        # "\n" only: the firmware stops at the first CR or LF, a CRLF would leave
        # a stray LF for the command parser.
        sock.sendall(hmac.new(_fw_key(password), nonce, hashlib.sha256).hexdigest().encode() + b"\n")
        reply, rest = _read_line(sock, rest, deadline, "login reply")
        if reply != b"OK":
            raise LoginError("login refused (%s); wrong password%s?"
                             % (reply.decode(errors="replace")[:20], " in " + label if label else ""))
        return rest
    if first == b"OK":
        return rest
    # Any other first line: an open console without the OK line. Hand it on.
    return first + b"\n" + rest


class Console:
    """One console connection. `password=None` looks it up via `password_for`."""

    def __init__(self, host: str, port: int = PORT, call: Optional[str] = None,
                 password: Optional[str] = None, connect_timeout: float = 10.0,
                 login_timeout: float = 5.0) -> None:
        self.host, self.port = host, port
        self.call = call or call_from_host(host)
        self.password = password if password is not None else password_for(self.call)
        self.connect_timeout, self.login_timeout = connect_timeout, login_timeout
        self.sock: Optional[socket.socket] = None

    @property
    def label(self) -> str:
        if OVERRIDE_VAR in os.environ:
            return OVERRIDE_VAR
        return pw_key(self.call) if self.call else "MC_PW_<CALL> (pass --call)"

    def connect(self) -> bytes:
        """Connect and log in; returns what arrived after the greeting."""
        self.close()
        sock = socket.create_connection((self.host, self.port), timeout=self.connect_timeout)
        # Keepalive lets the kernel notice a peer that vanished without a FIN
        # (node WLAN outage). macOS names the idle knob TCP_KEEPALIVE.
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
        for opt, val in (("TCP_KEEPIDLE", 30), ("TCP_KEEPINTVL", 10),
                         ("TCP_KEEPCNT", 3), ("TCP_KEEPALIVE", 30)):
            if hasattr(socket, opt):
                try:
                    sock.setsockopt(socket.IPPROTO_TCP, getattr(socket, opt), val)
                except OSError:
                    pass
        sock.settimeout(0.5)
        try:
            rest = login(sock, self.password, self.login_timeout, self.label)
        except BaseException:
            sock.close()
            raise
        self.sock = sock
        return rest

    def send(self, command: str) -> None:
        assert self.sock is not None, "send() before connect()"
        self.sock.sendall(command.encode() + b"\n")

    def close(self) -> None:
        if self.sock is not None:
            try:
                self.sock.close()
            except OSError:
                pass
            self.sock = None

    def __enter__(self) -> "Console":
        self.connect()
        return self

    def __exit__(self, *exc) -> None:
        self.close()


def read_info(host: str, call: Optional[str] = None, port: int = PORT, timeout: float = 4.0,
              password: Optional[str] = None) -> str:
    """Send ``--info`` and return everything the console printed for `timeout` s."""
    c = Console(host, port, call=call, password=password)
    out = c.connect()
    try:
        c.send("--info")
        assert c.sock is not None
        c.sock.settimeout(0.3)
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            try:
                d = c.sock.recv(4096)
            except socket.timeout:
                continue
            if not d:
                break
            out += d
    finally:
        c.close()
    return out.decode(errors="replace")
