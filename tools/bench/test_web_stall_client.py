#!/usr/bin/env python3
"""Unit tests for tools/bench/web_stall_client.py (localhost fixtures only, no hardware)."""

from __future__ import annotations

import hashlib
import hmac
import json
import socket
import sys
import threading
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import web_stall_client as wsc  # noqa: E402


def test_parse_args_defaults() -> None:
    a = wsc.parse_args(["10.0.0.1"])
    assert (a.mode, a.page, a.hold, a.rate, a.rcvbuf) == ("stall", "neighbours", 20.0, 200, 1024)
    assert wsc.parse_args(["h", "--trickle", "--rate", "50"]).mode == "trickle"
    assert wsc.parse_args(["h", "--normal", "--no-console"]).no_console


def test_page_path_and_request() -> None:
    assert wsc.page_path("neighbours") == "/?page=neighbours"
    assert wsc.page_path("/x?y=1") == "/x?y=1"
    assert wsc.build_request("/?page=path", "h").startswith(b"GET /?page=path HTTP/1.1\r\n")


def test_parse_web_lines() -> None:
    a = wsc.parse_web_line("[WEB];tx;abort;stall;sent;4096;ms;5000")
    assert a == {"kind": "abort", "reason": "stall", "sent": 4096, "ms": 5000}
    d = wsc.parse_web_line("12:00 [WEB];tx;done;sent;9000;ms;120;flushes;7")
    assert d == {"kind": "done", "sent": 9000, "ms": 120, "flushes": 7}
    assert wsc.parse_web_line("[WEB];tx;abort;;sent;1") is None
    assert wsc.parse_web_line("[HEAP];1;2") is None


@pytest.mark.parametrize(
    "line,expected",
    [
        ("[BOOT] RESET_REASON=6 TASK_WDT", True),
        ("[BOOT] LAST_LOOP_SECTION=web entered_ms=1", True),
        ("[BOOT];ready;ms;5000;ip;1", True),
        ("rst:0x1 (POWERON_RESET)", True),
        ("[WEB];tx;done;sent;1;ms;2;flushes;1", False),
        ("[HEAP];00:00:03;1;2;3", False),
    ],
)
def test_boot_detection(line: str, expected: bool) -> None:
    assert wsc.is_boot_line(line) is expected


def test_verdict_and_summarize() -> None:
    assert wsc.make_verdict(False, None, 10, 5) == "VERDICT;survived;abort=none;bytes=10;ms=5"
    assert wsc.make_verdict(True, "stall", 1, 2) == "VERDICT;rebooted;abort=stall;bytes=1;ms=2"
    s = wsc.summarize(["x", "[WEB];tx;abort;budget;sent;1;ms;2", "[BOOT] RESET_REASON=6 TASK_WDT"])
    assert s["abort"] == "budget" and len(s["boot_lines"]) == 1


def _serve(handler) -> tuple[int, threading.Thread]:
    srv = socket.socket()
    srv.bind(("127.0.0.1", 0))
    srv.listen(5)

    def loop() -> None:
        srv.settimeout(10)
        try:
            while True:
                c, _ = srv.accept()
                handler(c)
        except OSError:
            pass
        finally:
            srv.close()

    t = threading.Thread(target=loop, daemon=True)
    t.start()
    return srv.getsockname()[1], t


def _http(c: socket.socket) -> None:
    c.recv(1024)
    c.sendall(b"HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n" + b"x" * 5000)
    c.close()


def test_run_request_normal_and_probe() -> None:
    port, _ = _serve(_http)
    n, ms, err = wsc.run_request("127.0.0.1", port, "/?page=path", "normal", 0, 0, 1024, 5)
    assert err is None and n > 5000 and ms >= 0
    assert wsc.probe_http("127.0.0.1", port, 3)


def test_probe_fails_on_closed_port() -> None:
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    assert not wsc.probe_http("127.0.0.1", port, 1)


def test_console_hmac_login_and_capture() -> None:
    nonce = bytes(range(16))

    def handler(c: socket.socket) -> None:
        c.sendall(b"NONCE: " + nonce.hex().encode() + b"\n")
        got = c.recv(128).strip()
        ok = hmac.compare_digest(got, hmac.new(b"pw", nonce, hashlib.sha256).hexdigest().encode())
        c.sendall(b"OK\n" if ok else b"NO\n")
        if ok:
            c.sendall(b"[WEB];tx;done;sent;5;ms;1;flushes;1\n")
        c.close()  # peer close -> dropped

    port, _ = _serve(handler)
    con = wsc.Console("127.0.0.1", "pw", port)
    con.connect()
    import time

    for _ in range(50):
        if con.dropped:
            break
        time.sleep(0.1)
    assert con.dropped
    assert any("[WEB];tx;done" in ln for ln in con.snapshot())
    con.close()
    with pytest.raises(wsc.StallError):
        wsc.Console("127.0.0.1", "wrong", _serve(handler)[0]).connect()


def test_main_no_console_json(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    port, _ = _serve(_http)
    out = tmp_path / "s.json"
    rc = wsc.main(["127.0.0.1", "--normal", "--no-console", "--http-port", str(port), "--json", str(out)])
    assert rc == wsc.EXIT_SURVIVED
    assert capsys.readouterr().out.startswith("VERDICT;survived;abort=none;bytes=")
    data = json.loads(out.read_text())
    assert data["rebooted"] is False and data["http_alive"] is True
