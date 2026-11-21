#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = []
# ///
"""AppLink bench client: the MeshCom phone protocol over a LAN WebSocket.

Speaks `docs/applink-api-contract.md` (v1): ws://HOST:8002/applink, subprotocol
`meshcom-applink.1`, HMAC-SHA256 challenge-response login, application-level
heartbeat. Stdlib only (socket, hashlib, hmac): the `websockets` package is not
declared anywhere in the repo and is not installed in the tool environments.

    python3 tools/bench/applink_client.py 192.0.2.10 --hello --time \\
        --cmd "--info" --capture 10 --out /path/applink-frames.bin

The password comes from the environment variable named by `--password-env`
(default MESHCOM_PASSWD) or a getpass prompt. It is never accepted on the
command line and never printed.

`--out FILE` writes the received binary frames as `<u16 LE length><frame>`
records, the same format `ble_golden.py` stores in `ble-frames.bin`, plus
`FILE.txt` with one row per frame in the `ble-frames.txt` layout. Frames keep
the two trailing zero bytes the node appends (contract 4.2).

Exit codes: 0 clean, 2 login failed (4401), 3 locked out (4429), 4 AppLink
disabled (4403), 5 any other failure or close.
"""

from __future__ import annotations

import argparse
import base64
import getpass
import hashlib
import hmac
import json
import os
import socket
import struct
import sys
import time
from typing import Iterable

SUBPROTOCOL = "meshcom-applink.1"
MAC_PREFIX = b"MCAPPLINK1"
WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
LOGIN_TIMEOUT = 10.0

OP_HELLO = 0x10
OP_TIMESYNC = 0x20
OP_TEXT = 0xA0

NOTIFY_FLAGS = {0x40: "text/pos", 0x44: "json", 0x91: "mheard", 0x41: "ack"}

# contract section 7
CLOSE_MEANING = {
    1000: "normal close",
    1001: "node going away",
    1009: "message too big",
    4401: "login failed or binary before login",
    4403: "AppLink disabled or no password set",
    4408: "login timeout or heartbeat timeout",
    4409: "session taken over by a newer login",
    4429: "locked out after repeated login failures",
}
EXIT_FOR_CLOSE = {1000: 0, 4401: 2, 4429: 3, 4403: 4}


class AppLinkError(Exception):
    """Protocol or transport failure; `code` is the process exit code."""

    def __init__(self, message: str, code: int = 5) -> None:
        super().__init__(message)
        self.code = code


class Closed(Exception):
    def __init__(self, code: int, reason: str = "") -> None:
        super().__init__(f"closed {code}")
        self.code = code
        self.reason = reason


# --------------------------------------------------------------- frames


def compute_mac(password: str, nonce: bytes) -> str:
    """HMAC-SHA256(password without trailing spaces, "MCAPPLINK1" + nonce), hex."""
    key = password.rstrip(" ").encode("utf-8")
    return hmac.new(key, MAC_PREFIX + nonce, hashlib.sha256).hexdigest()


def build_hello(pin: str | None = None) -> bytes:
    if pin is None:
        return bytes([0x04, OP_HELLO, 0x20, 0x30])
    digest = hashlib.sha256(pin.encode("ascii")).digest()
    return bytes([0x23, OP_HELLO, 0x20, 0x30]) + digest


def build_time(epoch: int | None = None) -> bytes:
    return bytes([0x06, OP_TIMESYNC]) + struct.pack("<i", int(time.time()) if epoch is None
                                                    else epoch)


def build_cmd(text: str) -> bytes:
    payload = text.encode("utf-8")
    if len(payload) + 2 > 255:
        raise AppLinkError("command too long for a one-byte length")
    return bytes([len(payload) + 2, OP_TEXT]) + payload


def decode_frame(frame: bytes) -> str:
    """Short decode in the style of ble_golden.decode_notify, plus the content."""
    if not frame:
        return "empty"
    flag = frame[0]
    name = NOTIFY_FLAGS.get(flag, f"0x{flag:02x}")
    if len(frame) > 1 and flag in (0x40, 0x44):
        sub = chr(frame[1]) if 32 <= frame[1] < 127 else str(frame[1])
        name = f"{name}/{sub}"
    body = frame[1:].rstrip(b"\x00")
    if flag == 0x44:
        start, end = body.find(b"{"), body.rfind(b"}")
        if start >= 0 and end > start:
            return f"{name} {body[start:end + 1].decode('utf-8', 'replace')}"
    if flag == 0x40:
        text = body.decode("utf-8", "replace")
        return f"{name} " + "".join(c if c.isprintable() else "." for c in text)
    return name


def frame_row(t: float, frame: bytes) -> str:
    """Same columns as ble_golden.Capture._row."""
    short = decode_frame(frame).split(" ", 1)[0]
    return f"{t:9.3f} {'frame':11} len={len(frame):3d} {short:12} {frame.hex()}"


# ------------------------------------------------------------ websocket


class WsClient:
    """Minimal RFC 6455 client: handshake, masked frames, ping/pong, close."""

    def __init__(self, host: str, port: int, path: str = "/applink",
                 timeout: float = 5.0) -> None:
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.buf = b""
        self._handshake(host, port, path)

    def _handshake(self, host: str, port: int, path: str) -> None:
        key = base64.b64encode(os.urandom(16)).decode()
        req = (f"GET {path} HTTP/1.1\r\nHost: {host}:{port}\r\nUpgrade: websocket\r\n"
               f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\n"
               f"Sec-WebSocket-Version: 13\r\nSec-WebSocket-Protocol: {SUBPROTOCOL}\r\n\r\n")
        self.sock.sendall(req.encode())
        while b"\r\n\r\n" not in self.buf:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise AppLinkError("connection closed during handshake")
            self.buf += chunk
        head, _, self.buf = self.buf.partition(b"\r\n\r\n")
        lines = head.decode("latin-1").split("\r\n")
        if len(lines[0].split()) < 2 or lines[0].split()[1] != "101":
            raise AppLinkError(f"upgrade refused: {lines[0]}")
        hdr = {k.strip().lower(): v.strip()
               for k, _, v in (ln.partition(":") for ln in lines[1:])}
        want = base64.b64encode(hashlib.sha1((key + WS_GUID).encode()).digest()).decode()
        if hdr.get("sec-websocket-accept") != want:
            raise AppLinkError("bad Sec-WebSocket-Accept")
        if hdr.get("sec-websocket-protocol") != SUBPROTOCOL:
            raise AppLinkError("node did not echo subprotocol " + SUBPROTOCOL)

    def _send(self, opcode: int, payload: bytes) -> None:
        n = len(payload)
        head = bytes([0x80 | opcode])
        if n < 126:
            head += bytes([0x80 | n])
        elif n < 65536:
            head += bytes([0x80 | 126]) + struct.pack(">H", n)
        else:
            head += bytes([0x80 | 127]) + struct.pack(">Q", n)
        mask = os.urandom(4)
        body = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        self.sock.sendall(head + mask + body)

    def send_text(self, text: str) -> None:
        self._send(0x1, text.encode("utf-8"))

    def send_binary(self, data: bytes) -> None:
        self._send(0x2, data)

    def close(self, code: int = 1000) -> None:
        try:
            self._send(0x8, struct.pack(">H", code))
        except OSError:
            pass
        try:
            self.sock.close()
        except OSError:
            pass

    def _need(self, n: int) -> bytes:
        while len(self.buf) < n:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise Closed(1006, "connection lost")
            self.buf += chunk
        out, self.buf = self.buf[:n], self.buf[n:]
        return out

    def recv(self, timeout: float) -> tuple[str, bytes] | None:
        """Next data message ("text"/"binary", payload); None on timeout.

        Raises Closed on a close frame. Pings are answered here.
        """
        self.sock.settimeout(max(timeout, 0.001))
        msg = bytearray()
        kind = ""
        try:
            while True:
                b0, b1 = self._need(2)
                opcode = b0 & 0x0F
                n = b1 & 0x7F
                if n == 126:
                    (n,) = struct.unpack(">H", self._need(2))
                elif n == 127:
                    (n,) = struct.unpack(">Q", self._need(8))
                mask = self._need(4) if b1 & 0x80 else b""
                data = self._need(n)
                if mask:
                    data = bytes(b ^ mask[i % 4] for i, b in enumerate(data))
                if opcode == 0x8:
                    code = struct.unpack(">H", data[:2])[0] if len(data) >= 2 else 1005
                    raise Closed(code, data[2:].decode("utf-8", "replace"))
                if opcode == 0x9:
                    self._send(0xA, data)
                    continue
                if opcode == 0xA:
                    continue
                if opcode in (0x1, 0x2):
                    kind = "text" if opcode == 0x1 else "binary"
                msg += data
                if b0 & 0x80:
                    return kind, bytes(msg)
        except (socket.timeout, TimeoutError):
            return None


# --------------------------------------------------------------- session


def login(ws: WsClient, password: str, out) -> dict:
    deadline = time.monotonic() + LOGIN_TIMEOUT
    chal: dict | None = None
    while chal is None:
        got = ws.recv(deadline - time.monotonic())
        if got is None:
            raise AppLinkError("login timeout waiting for chal")
        kind, payload = got
        if kind == "text":
            obj = json.loads(payload)
            if obj.get("t") == "chal":
                chal = obj
    print(f"node: call={chal.get('call')} fw={chal.get('fw')} hw={chal.get('hw')} "
          f"v={chal.get('v')}", file=out)
    ws.send_text(json.dumps({"t": "auth", "mac": compute_mac(password,
                                                             bytes.fromhex(chal["nonce"]))}))
    while True:
        got = ws.recv(deadline - time.monotonic())
        if got is None:
            raise AppLinkError("login timeout waiting for ok")
        kind, payload = got
        if kind != "text":
            continue
        obj = json.loads(payload)
        if obj.get("t") == "ok":
            print(f"login ok, mtu={obj.get('mtu')}", file=out)
            return obj
        if obj.get("t") == "err":
            print(f"login refused: {obj.get('code')}", file=out)
            # the close frame that follows carries the authoritative code
            deadline = time.monotonic() + 2.0


def run(args: argparse.Namespace, password: str, out=None) -> int:
    out = out or sys.stdout
    try:
        ws = WsClient(args.host, args.port)
    except (OSError, AppLinkError) as exc:
        print(f"connect failed: {exc}", file=sys.stderr)
        return 5
    frames: list[tuple[float, bytes]] = []
    code = 0
    try:
        try:
            login(ws, password, out)
        except Closed as exc:
            return _report_close(exc, out)
        t0 = time.monotonic()
        sends: list[tuple[str, bytes]] = []
        if args.hello or args.pin_hash:
            pin = os.environ.get(args.pin_env, "000000") if args.pin_hash else None
            sends.append(("hello", build_hello(pin)))
        if args.time:
            sends.append(("time", build_time()))
        for cmd in args.cmd:
            sends.append((f"cmd {cmd}", build_cmd(cmd)))
        for label, frame in sends:
            ws.send_binary(frame)
            if args.json:
                print(json.dumps({"dir": "tx", "label": label, "hex": frame.hex()}), file=out)
            else:
                print(f"{time.monotonic() - t0:9.3f} send        {label}", file=out)
        end = t0 + args.capture
        try:
            while True:
                left = end - time.monotonic()
                if left <= 0:
                    break
                got = ws.recv(left)
                if got is None:
                    break
                kind, payload = got
                t = time.monotonic() - t0
                if kind == "text":
                    try:
                        obj = json.loads(payload)
                    except ValueError:
                        continue
                    if obj.get("t") == "hb":
                        ws.send_text('{"t":"hb"}')
                    elif obj.get("t") == "bye":
                        print(f"bye: {obj.get('reason')}", file=out)
                    continue
                frames.append((t, payload))
                if args.json:
                    print(json.dumps({"dir": "rx", "t": round(t, 3), "len": len(payload),
                                      "hex": payload.hex(), "decode": decode_frame(payload)}),
                          file=out)
                else:
                    print(frame_row(t, payload) + "  " + decode_frame(payload), file=out)
        except Closed as exc:
            code = _report_close(exc, out)
        else:
            ws.close(1000)
    except Closed as exc:
        code = _report_close(exc, out)
    except AppLinkError as exc:
        print(f"error: {exc}", file=sys.stderr)
        code = exc.code
        ws.close(1000)
    except OSError as exc:
        print(f"error: {exc}", file=sys.stderr)
        code = 5
    finally:
        if args.out:
            write_capture(args.out, frames)
    return code


def _report_close(exc: Closed, out) -> int:
    meaning = CLOSE_MEANING.get(exc.code, "unknown")
    print(f"closed by node: {exc.code} ({meaning})", file=out)
    return EXIT_FOR_CLOSE.get(exc.code, 5)


def write_capture(path: str, frames: list[tuple[float, bytes]]) -> None:
    with open(path, "wb") as fh:
        for _, frame in frames:
            fh.write(struct.pack("<H", len(frame)) + frame)
    with open(path + ".txt", "w", encoding="utf-8") as fh:
        for t, frame in frames:
            fh.write(frame_row(t, frame) + "\n")


def main(argv: Iterable[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="MeshCom AppLink bench client")
    ap.add_argument("host")
    ap.add_argument("--port", type=int, default=8002)
    ap.add_argument("--password-env", default="MESHCOM_PASSWD",
                    help="environment variable holding the node password (default MESHCOM_PASSWD)")
    ap.add_argument("--hello", action="store_true", help="send hello 04 10 20 30")
    ap.add_argument("--pin-hash", action="store_true",
                    help="send the 36-byte hello with a PIN hash (PIN from --pin-env, "
                         "default 000000); the node ignores it on AppLink")
    ap.add_argument("--pin-env", default="MESHCOM_PIN")
    ap.add_argument("--time", action="store_true", help="send the 0x20 timestamp frame (now)")
    ap.add_argument("--cmd", action="append", default=[], metavar="TEXT",
                    help='send a 0xA0 text/command frame, e.g. --cmd "--info" (repeatable)')
    ap.add_argument("--capture", type=float, default=10.0, help="seconds to listen (default 10)")
    ap.add_argument("--out", help="write raw frames (ble-frames.bin format) to FILE, rows to "
                                  "FILE.txt")
    ap.add_argument("--json", action="store_true", help="print one JSON object per event")
    args = ap.parse_args(list(argv) if argv is not None else None)

    password = os.environ.get(args.password_env)
    if not password:
        password = getpass.getpass(f"Node password (${args.password_env} not set): ")
    return run(args, password)


if __name__ == "__main__":
    sys.exit(main())
