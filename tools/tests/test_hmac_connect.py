"""HMAC-WIN-01: tools/hmac_connect.py shutdown paths.

No Windows machine on the bench: the Windows case is simulated by removing signal.pthread_kill, which Windows
does not have. The real Windows console path (GenerateConsoleCtrlEvent) is not exercised here.
"""

import importlib.util
import io
import os
import signal
import socket
import sys
import threading
import time
from pathlib import Path

import pytest

SCRIPT = Path(__file__).resolve().parents[1] / "hmac_connect.py"


def load():
    spec = importlib.util.spec_from_file_location("hmac_connect_under_test", SCRIPT)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


@pytest.fixture
def restore_signals():
    sigs = [signal.SIGINT, signal.SIGTERM, signal.SIGHUP]
    saved = {s: signal.getsignal(s) for s in sigs}
    yield
    for s, h in saved.items():
        signal.signal(s, h)


@pytest.fixture
def thread_errors(monkeypatch):
    errors = []
    monkeypatch.setattr(threading, "excepthook", lambda a: errors.append(a.exc_type))
    return errors


def serve_once(payload, close_after=0.3):
    """One-shot console stand-in: sends payload, waits, closes. Returns (host, port)."""
    srv = socket.socket()
    srv.bind(("127.0.0.1", 0))
    srv.listen(1)

    def run():
        conn, _ = srv.accept()
        conn.sendall(payload)
        time.sleep(close_after)
        conn.close()
        srv.close()

    threading.Thread(target=run, daemon=True).start()
    return srv.getsockname()


class Args:
    def __init__(self, host, port):
        self.host, self.port = host, port
        self.password = None
        self.ask = False
        self.log = None
        self.no_timestamps = False


def blocking_input(limit=30.0):
    """Stands in for input(): blocks like a console read, in small steps so a pending interrupt is delivered."""

    def fake(*_a, **_k):
        end = time.monotonic() + limit
        while time.monotonic() < end:
            time.sleep(0.02)
        raise EOFError  # never woken: what the user sees as a hang

    return fake


def run_until_woken(mod, args, sess):
    try:
        mod.run(args, sess)
    except KeyboardInterrupt:
        return True
    finally:
        mod.cleanup(sess)
    return False


def test_remote_close_wakes_main_without_pthread_kill(monkeypatch, restore_signals, thread_errors):
    mod = load()
    monkeypatch.delattr(signal, "pthread_kill", raising=False)  # Windows
    monkeypatch.setattr("builtins.input", blocking_input())
    host, port = serve_once(b"OK\r\nMeshCom console\r\n")
    sess = mod.Session()
    woken = run_until_woken(mod, Args(host, port), sess)
    assert thread_errors == []
    assert woken, "main thread stayed in input() after the remote closed"
    assert sess.close_reason == "disconnected by remote"


def test_stdout_encoding_error_does_not_kill_receive_thread(monkeypatch, restore_signals, thread_errors, capsys):
    mod = load()
    with capsys.disabled():
        raw = io.BytesIO()
        out = io.TextIOWrapper(raw, encoding="ascii", errors="strict", write_through=True)
        monkeypatch.setattr(sys, "stdout", out)
        monkeypatch.setattr("builtins.input", blocking_input())
        host, port = serve_once("OK\r\nGrüße 73\r\n".encode("utf-8"))
        sess = mod.Session()
        woken = run_until_woken(mod, Args(host, port), sess)
        text = raw.getvalue().decode("ascii")
    assert thread_errors == []
    assert woken, "receive thread died on the encoding error and never woke the main thread"
    assert "Gr??e 73" in text
    assert sess.close_reason == "disconnected by remote"


def test_second_sigterm_during_cleanup_still_closes(tmp_path, restore_signals):
    mod = load()
    for sig in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
        signal.signal(sig, mod._raise_keyboard_interrupt)  # as main() installs them

    class Sock:
        closed = False

        def shutdown(self, how):
            pass

        def close(self):
            self.closed = True

    class RxThread:
        def join(self, timeout=None):
            os.kill(os.getpid(), signal.SIGTERM)  # the second signal arrives while cleanup waits
            time.sleep(0.05)

    sess = mod.Session()
    sess.sock = Sock()
    sess.rx_thread = RxThread()
    log_path = tmp_path / "s.log"
    sess.log = mod.SessionLog(str(log_path))
    sess.close_reason = "session closed by user (interrupt)"
    try:
        mod.cleanup(sess)
    except KeyboardInterrupt:
        pytest.fail("second signal interrupted cleanup")
    assert sess.sock.closed
    assert sess.log.closed
    assert "session closed by user (interrupt)" in log_path.read_text()
