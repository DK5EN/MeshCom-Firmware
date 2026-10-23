"""Tests for tools/mc_console.py (net console 2323 login, .env password lookup)
and the clients built on it.

FakeNode reproduces the auth path of src/net_console.cpp authTask byte for byte:
NONCE line, response read up to the first CR or LF (71 chars max), FAIL + close
or the OK banner. Everything received after the login is recorded, so a stray
byte left over from the response shows up.
"""

from __future__ import annotations

import hashlib
import hmac
import os
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path

import pytest

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
sys.path.insert(0, str(TOOLS / "bench"))

import mc_console  # noqa: E402

BANNER = b"OK\r\nMeshCom Console\r\nType --help for commands\r\n"


class FakeNode:
    def __init__(self, password: str = "", chatty: bool = False, close_at_once: bool = False) -> None:
        self.password = password
        self.chatty = chatty
        self.close_at_once = close_at_once
        self.auth: list[bool] = []
        self.rx = b""
        self.srv = socket.socket()
        self.srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.srv.bind(("127.0.0.1", 0))
        self.srv.listen(4)
        self.port = self.srv.getsockname()[1]
        self.stop = threading.Event()
        threading.Thread(target=self._accept, daemon=True).start()

    def close(self) -> None:
        self.stop.set()
        self.srv.close()

    def _accept(self) -> None:
        while not self.stop.is_set():
            try:
                c, _ = self.srv.accept()
            except OSError:
                return
            threading.Thread(target=self._client, args=(c,), daemon=True).start()

    def _client(self, c: socket.socket) -> None:
        try:
            if self.close_at_once:  # "Rejected: auth already in progress"
                return
            if self.password:
                nonce = os.urandom(16)
                c.sendall(b"NONCE: " + nonce.hex().encode() + b"\r\n")
                c.settimeout(5)
                buf, done = b"", False
                while len(buf) < 71:
                    ch = c.recv(1)
                    if not ch:
                        break
                    if ch in (b"\r", b"\n"):
                        done = True
                        break
                    buf += ch
                want = hmac.new(self.password.encode(), nonce, hashlib.sha256).hexdigest().encode()
                ok = done and hmac.compare_digest(buf, want)
                self.auth.append(ok)
                if not ok:
                    c.sendall(b"FAIL\r\n")
                    return
            c.sendall(BANNER)
            c.settimeout(0.1)
            n, last = 0, 0.0
            while not self.stop.is_set():
                if self.chatty and time.time() - last > 0.1:
                    n, last = n + 1, time.time()
                    c.sendall(b"[LOG ] chatter %d\r\n" % n)
                try:
                    d = c.recv(1024)
                except socket.timeout:
                    continue
                if not d:
                    return
                self.rx += d
                if b"--info" in d:
                    c.sendall(b"...Call: DK5EN-92 ...\r\n")
        except OSError:
            pass
        finally:
            c.close()


@pytest.fixture
def no_env(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> Path:
    """No inherited passwords; MC_ENV_FILE points at an empty temp file."""
    for k in list(os.environ):
        if k.startswith(mc_console.KEY_PREFIX) or k in (mc_console.OVERRIDE_VAR, "RM_PASSWD"):
            monkeypatch.delenv(k)
    env = tmp_path / ".env"
    env.write_text("")
    env.chmod(0o600)
    monkeypatch.setenv(mc_console.ENV_FILE_VAR, str(env))
    return env


@pytest.fixture
def node():
    nodes: list[FakeNode] = []

    def make(**kw) -> FakeNode:
        n = FakeNode(**kw)
        nodes.append(n)
        return n

    yield make
    for n in nodes:
        n.close()


# -- login -------------------------------------------------------------------

def test_login_with_password_keeps_banner_and_sends_no_stray_byte(no_env, node) -> None:
    n = node(password="benchpw")
    c = mc_console.Console("127.0.0.1", n.port, password="benchpw")
    rest = c.connect()
    assert n.auth == [True]
    assert b"MeshCom Console" in rest  # the banner follows OK in the same send
    c.send("--info")
    time.sleep(0.3)
    c.close()
    # the firmware reads the response up to the first CR/LF; a CRLF would leave
    # an LF that reaches the command parser
    assert n.rx == b"--info\n"


def test_open_console_with_and_without_password(no_env, node) -> None:
    for pw in ("", "unused"):
        n = node()
        c = mc_console.Console("127.0.0.1", n.port, password=pw)
        assert b"MeshCom Console" in c.connect()
        c.close()


def test_nonce_without_password_names_the_key(no_env, node) -> None:
    n = node(password="benchpw")
    with pytest.raises(mc_console.LoginError, match="MC_PW_DK5EN_92"):
        mc_console.Console("127.0.0.1", n.port, call="DK5EN-92").connect()
    assert n.auth == []  # nothing was sent: no blind HMAC attempt


def test_wrong_password_is_a_refusal_and_never_echoes_it(no_env, node) -> None:
    n = node(password="benchpw")
    with pytest.raises(mc_console.LoginError) as e:
        mc_console.Console("127.0.0.1", n.port, password="geheim-falsch").connect()
    assert "geheim-falsch" not in str(e.value)
    assert n.auth == [False]


def test_dropped_connection_is_transient_not_a_refusal(no_env, node) -> None:
    n = node(close_at_once=True)
    with pytest.raises(ConnectionError) as e:
        mc_console.Console("127.0.0.1", n.port).connect()
    assert not isinstance(e.value, mc_console.LoginError)


def test_password_normalised_like_the_firmware(no_env, node) -> None:
    # node_passwd is "%-14.14s" with the padding stripped: 14 chars, no trailing blanks
    n = node(password="abcdefghijklmn")
    mc_console.Console("127.0.0.1", n.port, password="abcdefghijklmnXYZ").connect()
    n2 = node(password="pw")
    mc_console.Console("127.0.0.1", n2.port, password="pw   ").connect()
    assert n.auth == [True] and n2.auth == [True]


# -- password lookup ---------------------------------------------------------

def test_lookup_order(no_env, monkeypatch: pytest.MonkeyPatch) -> None:
    no_env.write_text('# c\nexport MC_PW_DK5EN_92="from-file"\nMC_PW_DK5EN_1=one\n')
    assert mc_console.password_for("DK5EN-92") == "from-file"
    assert mc_console.password_for(host="dk5en-1.local") == "one"
    assert mc_console.password_for(host="192.168.68.66") == ""
    assert mc_console.password_for("DK5EN-14") == ""
    monkeypatch.setenv("MC_PW_DK5EN_92", "from-env")
    assert mc_console.password_for("DK5EN-92") == "from-env"
    monkeypatch.setenv(mc_console.OVERRIDE_VAR, "")
    assert mc_console.password_for("DK5EN-92") == ""  # set-but-empty forces no password


def test_env_file_must_be_private(no_env) -> None:
    no_env.write_text("MC_PW_DK5EN_92=x\n")
    no_env.chmod(0o644)
    with pytest.raises(mc_console.EnvFileError, match="chmod 600"):
        mc_console.password_for("DK5EN-92")


def test_env_syntax_error_does_not_leak_the_line(no_env) -> None:
    no_env.write_text("this is secret-value\n")
    with pytest.raises(mc_console.EnvFileError) as e:
        mc_console.load_env()
    assert "secret" not in str(e.value) and ":1:" in str(e.value)


def test_standalone_deploy_reads_env_next_to_module(no_env, tmp_path: Path) -> None:
    """The rpizero copy: mc_console.py in a plain directory, .env beside it."""
    deploy = tmp_path / "meshlog"
    deploy.mkdir()
    (deploy / "mc_console.py").write_bytes((TOOLS / "mc_console.py").read_bytes())
    env = deploy / ".env"
    env.write_text("MC_PW_DK5EN_92=beside\n")
    env.chmod(0o600)
    clean = {k: v for k, v in os.environ.items() if k != mc_console.ENV_FILE_VAR}
    r = subprocess.run([sys.executable, "-I", "-c",
                        "import sys; sys.path.insert(0, %r); import mc_console as m; "
                        "print(m.password_for('DK5EN-92'))" % str(deploy)],
                       env=clean, capture_output=True, text=True, timeout=10)
    assert r.returncode == 0, r.stderr
    assert r.stdout.strip() == "beside"


def test_repo_env_is_gitignored() -> None:
    repo = TOOLS.parent
    r = subprocess.run(["git", "check-ignore", "-q", ".env"], cwd=repo)
    assert r.returncode == 0
    r = subprocess.run(["git", "check-ignore", "-q", ".env.example"], cwd=repo)
    assert r.returncode == 1


# -- clients: a refused login must be loud (each was silent before) -------------

def test_console_golden_refused_login_raises(no_env, node) -> None:
    import console_golden

    n = node(password="benchpw")
    no_env.write_text("MC_PW_DK5EN_92=wrong\n")
    with pytest.raises(mc_console.LoginError):
        console_golden.TcpTransport("127.0.0.1", n.port, "DK5EN-92")
    assert n.auth == [False]  # one attempt, no retries with the same bad password


def test_tdeck_tcp_console_uses_env_password(no_env, node) -> None:
    import tdeck_harness

    n = node(password="benchpw")
    no_env.write_text("MC_PW_DK5EN_14=benchpw\n")
    c = tdeck_harness._TcpConsole("tcp://127.0.0.1:%d" % n.port, "DK5EN-14")
    c.write(b"--info\n")
    deadline = time.time() + 2
    got = b""
    while b"DK5EN-92" not in got and time.time() < deadline:
        got += c.readline()
    c.close()
    assert b"DK5EN-92" in got


def test_remote_cmd_sends_on_a_chatty_node(no_env, node) -> None:
    import remote_cmd

    n = node(password="benchpw", chatty=True)
    no_env.write_text("MC_PW_DK5EN_1=benchpw\n")
    t0 = time.monotonic()
    remote_cmd.send_via_console("127.0.0.1", "DK5EN-90", "hello", "DK5EN-1", port=n.port, listen_s=0.3)
    assert time.monotonic() - t0 < 4
    assert n.rx.startswith(b"::{DK5EN-90}hello")


def test_identity_guard_reads_info_with_fleet_call(no_env, node) -> None:
    import identity_guard

    n = node(password="benchpw")
    no_env.write_text("MC_PW_DK5EN_92=benchpw\n")
    assert "DK5EN-92" in identity_guard.read_info_net("127.0.0.1", timeout=0.5, port=n.port, call="DK5EN-92")
    no_env.write_text("MC_PW_DK5EN_92=stale\n")
    with pytest.raises(identity_guard.IdentityError, match="no longer DK5EN-92"):
        identity_guard.read_info_net("127.0.0.1", timeout=0.5, port=n.port, call="DK5EN-92")


def test_meshlogger_stops_on_refused_login(no_env, node, tmp_path: Path) -> None:
    n = node(password="benchpw")
    out = tmp_path / "con.log"
    env = dict(os.environ, MC_CONSOLE_PW="wrong")
    p = subprocess.run([sys.executable, str(TOOLS / "meshlogger.py"), "127.0.0.1", "--port", str(n.port),
                        "--no-debug-flags", "--seconds", "30", "--out", str(out)],
                       env=env, capture_output=True, timeout=20)
    assert p.returncode == 0
    log = out.read_text()
    assert "stopping: login refused" in log
    assert n.auth == [False]


def test_meshlogger_passive_capture_with_cmd(no_env, node, tmp_path: Path) -> None:
    """The netconsole_log.py / netlurk.py use: one file, a command, a fixed time."""
    n = node(password="benchpw")
    no_env.write_text("MC_PW_DK5EN_92=benchpw\n")
    out = tmp_path / "info.log"
    p = subprocess.run([sys.executable, str(TOOLS / "meshlogger.py"), "127.0.0.1", "--port", str(n.port),
                        "--call", "DK5EN-92", "--no-debug-flags", "--seconds", "2", "--out", str(out),
                        "--cmd=--info"], capture_output=True, timeout=20)
    assert p.returncode == 0, p.stderr
    log = out.read_text()
    assert "[LOGGER] >> --info" in log and "Call: DK5EN-92" in log
    assert n.rx == b"--info\n"
