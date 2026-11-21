"""Tests for tools/bench/applink_client.py against a local fake AppLink node."""

from __future__ import annotations

import argparse
import base64
import hashlib
import hmac
import importlib.util
import io
import json
import socket
import struct
import threading
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("applink_client",
                                              ROOT / "tools" / "bench" / "applink_client.py")
assert spec and spec.loader
ac = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ac)

NONCE = bytes(range(16))
FRAME_A = bytes([0x40, 0x3A]) + b"DK5EN-1>*:hi" + b"\x00\x00"
FRAME_B = bytes([0x44]) + b'D{"TYP":"DI","x":1}' + b"\x00\x00"


def _frame(opcode: int, payload: bytes) -> bytes:
    assert len(payload) < 126
    return bytes([0x80 | opcode, len(payload)]) + payload


class FakeNode(threading.Thread):
    """Contract node side, one connection. mode: ok | lock | disabled."""

    def __init__(self, password: str = "secret", mode: str = "ok") -> None:
        super().__init__(daemon=True)
        self.password = password
        self.mode = mode
        self.srv = socket.socket()
        self.srv.bind(("127.0.0.1", 0))
        self.srv.listen(1)
        self.port = self.srv.getsockname()[1]
        self.buf = b""
        self.got_binary: list[bytes] = []
        self.hb_answers = 0
        self.close_code: int | None = None
        self.subprotocol = ""

    def _need(self, n: int) -> bytes:
        while len(self.buf) < n:
            chunk = self.conn.recv(4096)
            if not chunk:
                raise EOFError
            self.buf += chunk
        out, self.buf = self.buf[:n], self.buf[n:]
        return out

    def _read(self) -> tuple[int, bytes]:
        b0, b1 = self._need(2)
        n = b1 & 0x7F
        assert b1 & 0x80, "client frames must be masked"
        if n == 126:
            (n,) = struct.unpack(">H", self._need(2))
        mask = self._need(4)
        data = bytes(b ^ mask[i % 4] for i, b in enumerate(self._need(n)))
        return b0 & 0x0F, data

    def _close(self, code: int) -> None:
        self.conn.sendall(_frame(0x8, struct.pack(">H", code)))

    def run(self) -> None:
        self.srv.settimeout(10)
        try:
            self.conn, _ = self.srv.accept()
            self.conn.settimeout(10)
            while b"\r\n\r\n" not in self.buf:
                self.buf += self.conn.recv(4096)
            head, _, self.buf = self.buf.partition(b"\r\n\r\n")
            hdr = {ln.split(":", 1)[0].strip().lower(): ln.split(":", 1)[1].strip()
                   for ln in head.decode().split("\r\n")[1:]}
            self.subprotocol = hdr["sec-websocket-protocol"]
            accept = base64.b64encode(hashlib.sha1(
                (hdr["sec-websocket-key"] + ac.WS_GUID).encode()).digest()).decode()
            self.conn.sendall(
                ("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                 f"Connection: Upgrade\r\nSec-WebSocket-Accept: {accept}\r\n"
                 f"Sec-WebSocket-Protocol: {ac.SUBPROTOCOL}\r\n\r\n").encode())
            if self.mode == "disabled":
                self._close(4403)
                return
            chal = {"t": "chal", "v": 1, "nonce": NONCE.hex(), "call": "DK5EN-1",
                    "fw": "4.35a.11.21", "hw": "HELTEC_V3"}
            self.conn.sendall(_frame(0x1, json.dumps(chal).encode()))
            _, auth = self._read()
            mac = json.loads(auth)["mac"]
            want = hmac.new(self.password.encode(), ac.MAC_PREFIX + NONCE,
                            hashlib.sha256).hexdigest()
            if not hmac.compare_digest(mac, want):
                self.conn.sendall(_frame(0x1, b'{"t":"err","code":"auth"}'))
                self._close(4429 if self.mode == "lock" else 4401)
                return
            self.conn.sendall(_frame(0x1, b'{"t":"ok","mtu":257}'))
            self.conn.sendall(_frame(0x1, b'{"t":"hb"}'))
            while True:  # the client sends its binary frames first, then answers the hb
                op, data = self._read()
                if op == 0x1 and json.loads(data).get("t") == "hb":
                    self.hb_answers += 1
                    break
                self.got_binary.append(data)
            self.conn.sendall(_frame(0x2, FRAME_A))
            self.conn.sendall(_frame(0x2, FRAME_B))
            self._close(1000)
            self.conn.settimeout(1)
            try:
                while True:
                    op, data = self._read()
                    if op == 0x2:
                        self.got_binary.append(data)
            except (EOFError, OSError):
                pass
        except (EOFError, OSError, AssertionError):
            pass
        finally:
            self.srv.close()


def _args(port: int, **kw) -> argparse.Namespace:
    base = dict(host="127.0.0.1", port=port, hello=False, pin_hash=False, pin_env="X",
                time=False, cmd=[], capture=3.0, out=None, json=False)
    base.update(kw)
    return argparse.Namespace(**base)


def _run(password: str = "secret", node_mode: str = "ok", node_pw: str = "secret",
         **kw) -> tuple[int, str, FakeNode]:
    node = FakeNode(node_pw, node_mode)
    node.start()
    buf = io.StringIO()
    code = ac.run(_args(node.port, **kw), password, out=buf)
    node.join(5)
    return code, buf.getvalue(), node


def test_mac_vector() -> None:
    assert ac.compute_mac("secret", NONCE) == \
        "d9682ef87ae8fab2245f1e213ad150c0c66f4d4eeae96883339c22f3f1e9a4c0"


def test_trailing_spaces_stripped() -> None:
    assert ac.compute_mac("secret   ", NONCE) == ac.compute_mac("secret", NONCE)


def test_frame_builders() -> None:
    assert ac.build_hello() == bytes([4, 0x10, 0x20, 0x30])
    assert ac.build_time(1767225600) == bytes([6, 0x20]) + struct.pack("<i", 1767225600)
    assert ac.build_cmd("--info") == bytes([8, 0xA0]) + b"--info"
    assert len(ac.build_hello("123456")) == 36


def test_decode() -> None:
    assert "DK5EN-1>*:hi" in ac.decode_frame(FRAME_A)
    assert ac.decode_frame(FRAME_B).endswith('{"TYP":"DI","x":1}')


def test_happy_path_capture(tmp_path: Path) -> None:
    out = tmp_path / "cap.bin"
    code, text, node = _run(out=str(out), hello=True)
    assert code == 0
    assert "call=DK5EN-1 fw=4.35a.11.21 hw=HELTEC_V3" in text
    assert "mtu=257" in text
    assert FRAME_A.hex() in text and FRAME_B.hex() in text
    assert "DK5EN-1>*:hi" in text and '{"TYP":"DI","x":1}' in text
    assert node.subprotocol == ac.SUBPROTOCOL
    blob = out.read_bytes()
    expected = b"".join(struct.pack("<H", len(f)) + f for f in (FRAME_A, FRAME_B))
    assert blob == expected
    rows = Path(str(out) + ".txt").read_text().splitlines()
    assert len(rows) == 2 and rows[0].endswith(FRAME_A.hex())
    assert node.hb_answers == 1 and node.got_binary == [ac.build_hello()]


def test_heartbeat_answered() -> None:
    code, _, node = _run()
    assert code == 0
    assert node.hb_answers == 1


def test_commands_sent() -> None:
    code, _, node = _run(cmd=["--info"], time=True, hello=True)
    assert code == 0
    assert node.hb_answers == 1
    assert len(node.got_binary) == 3
    assert node.got_binary[0] == ac.build_hello()
    assert node.got_binary[1][:2] == bytes([6, 0x20])
    assert node.got_binary[2] == ac.build_cmd("--info")


def test_wrong_password_exit_2() -> None:
    code, text, _ = _run(password="wrong")
    assert code == 2
    assert "4401" in text


def test_lockout_exit_3() -> None:
    code, text, _ = _run(password="wrong", node_mode="lock")
    assert code == 3
    assert "4429" in text


def test_disabled_exit_4() -> None:
    code, text, _ = _run(node_mode="disabled")
    assert code == 4
    assert "4403" in text


def test_trailing_space_password_logs_in() -> None:
    code, text, _ = _run(password="secret  ")
    assert code == 0
    assert "login ok" in text


def test_json_output() -> None:
    code, text, _ = _run(json=True)
    assert code == 0
    rx = [json.loads(ln) for ln in text.splitlines() if ln.startswith("{")]
    assert [r["hex"] for r in rx if r["dir"] == "rx"] == [FRAME_A.hex(), FRAME_B.hex()]
