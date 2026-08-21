#!/usr/bin/env python3
"""Unit tests for the mock MeshCom server (tools/mock/meshcom_server.py).

Stdlib unittest only -- pytest is NOT available in this environment.

Run with:
    python3 -m unittest discover tools/mock -v
    python3 tools/mock/test_mock_server.py
"""

from __future__ import annotations

import re
import socket
import struct
import sys
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import meshcom_server as srv  # noqa: E402
import mock_client as cli  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[2]
CORPUS_PATH = REPO_ROOT / "test" / "test_aprs_corpus" / "corpus.txt"

_CORPUS_LINE_RE = re.compile(r"^(?P<name>\S+)\s+(?P<hex>[0-9A-Fa-f]+)\s*$")


def load_corpus_frame(name: str) -> bytes:
    """Read one named raw frame out of test/test_aprs_corpus/corpus.txt."""
    with CORPUS_PATH.open("r", encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            m = _CORPUS_LINE_RE.match(line)
            if m and m.group("name") == name:
                return bytes.fromhex(m.group("hex"))
    raise AssertionError(f"corpus frame {name!r} not found in {CORPUS_PATH}")


def free_udp_socket(timeout: float = 2.0) -> socket.socket:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", 0))
    sock.settimeout(timeout)
    return sock


def wait_until(predicate, timeout: float = 1.0, interval: float = 0.01) -> bool:
    """Poll predicate() until it is truthy or timeout elapses (avoids
    fixed-sleep flakiness in the cross-thread registry checks below)."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return True
        time.sleep(interval)
    return bool(predicate())


class ServerTestCase(unittest.TestCase):
    """Starts a MockMeshComServer on an ephemeral port for each test."""

    def setUp(self) -> None:
        self.server = srv.MockMeshComServer(
            "127.0.0.1", 0, callsign="MOCK-SRV", verbose=False, registry_ttl=120.0
        )
        self.server.start()
        self.addCleanup(self.server.stop)

    @property
    def server_addr(self) -> tuple[str, int]:
        return ("127.0.0.1", self.server.port)

    def register(self, sock: socket.socket, gateway_id: int, callsign: str) -> None:
        sock.sendto(cli.build_keep(gateway_id, callsign), self.server_addr)
        sock.recvfrom(4096)  # BEAT


# ---------------------------------------------------------------------------
# 1. KEEP -> BEAT round-trip
# ---------------------------------------------------------------------------


class TestKeepBeatRoundTrip(ServerTestCase):
    def test_beat_byte_exact_without_status(self) -> None:
        node_sock = free_udp_socket()
        self.addCleanup(node_sock.close)

        keep = cli.build_keep(0x48A4690D, "DK5EN-90", groups=["232", "262"])
        node_sock.sendto(keep, self.server_addr)
        beat, _addr = node_sock.recvfrom(4096)

        call_b = b"MOCK-SRV"
        expected = b"BEAT" + b"\x00" + bytes([len(call_b)]) + call_b
        self.assertEqual(beat, expected)

    def test_beat_byte_exact_with_status(self) -> None:
        server = srv.MockMeshComServer(
            "127.0.0.1", 0, callsign="MOCK-SRV", beat_status="OK", verbose=False
        )
        server.start()
        self.addCleanup(server.stop)

        node_sock = free_udp_socket()
        self.addCleanup(node_sock.close)

        keep = cli.build_keep(0x48A4690D, "DK5EN-90")
        node_sock.sendto(keep, ("127.0.0.1", server.port))
        beat, _addr = node_sock.recvfrom(4096)

        call_b = b"MOCK-SRV"
        status_b = b"OK"
        expected = (
            b"BEAT"
            + b"\x00"
            + bytes([len(call_b)])
            + call_b
            + b"\x01"
            + bytes([len(status_b)])
            + status_b
        )
        self.assertEqual(beat, expected)

    def test_keep_registers_client(self) -> None:
        node_sock = free_udp_socket()
        self.addCleanup(node_sock.close)

        self.register(node_sock, 0x48A4690D, "DK5EN-90")

        wait_until(lambda: len(self.server.clients) == 1)
        clients = list(self.server.clients.values())
        self.assertEqual(len(clients), 1)
        self.assertEqual(clients[0].gateway_id, "48A4690D")
        self.assertEqual(clients[0].callsign, "DK5EN-90")


# ---------------------------------------------------------------------------
# 2. Registry + DATA redistribution as GATE (not echoed back to sender)
# ---------------------------------------------------------------------------


class TestDataRedistribution(ServerTestCase):
    def test_data_redistributed_to_other_client_only(self) -> None:
        sock_a = free_udp_socket()
        sock_b = free_udp_socket()
        self.addCleanup(sock_a.close)
        self.addCleanup(sock_b.close)

        self.register(sock_a, 0x11111111, "NODE-A")
        self.register(sock_b, 0x22222222, "NODE-B")
        wait_until(lambda: len(self.server.clients) == 2)

        frame = load_corpus_frame("f001")
        data = cli.build_data(0x11111111, "NODE-A", frame)
        sock_a.sendto(data, self.server_addr)

        gate, _addr = sock_b.recvfrom(4096)
        self.assertEqual(gate, b"GATE" + frame)

        # A must not receive its own frame back.
        sock_a.settimeout(0.3)
        with self.assertRaises(socket.timeout):
            sock_a.recvfrom(4096)


# ---------------------------------------------------------------------------
# 3. DATA header validation, using real corpus frames
# ---------------------------------------------------------------------------


class TestDataHeaderValidation(ServerTestCase):
    def setUp(self) -> None:
        super().setUp()
        self.sock_a = free_udp_socket()
        self.sock_b = free_udp_socket()
        self.addCleanup(self.sock_a.close)
        self.addCleanup(self.sock_b.close)
        self.register(self.sock_a, 0x11111111, "NODE-A")
        self.register(self.sock_b, 0x22222222, "NODE-B")
        wait_until(lambda: len(self.server.clients) == 2)

    def test_accepts_and_forwards_real_corpus_frames(self) -> None:
        for name in ("f001", "f006"):
            with self.subTest(frame=name):
                frame = load_corpus_frame(name)
                data = cli.build_data(0x11111111, "NODE-A", frame)
                self.sock_a.sendto(data, self.server_addr)
                gate, _addr = self.sock_b.recvfrom(4096)
                self.assertEqual(gate, b"GATE" + frame)

    def test_rejects_wrong_length_header(self) -> None:
        short_data = b"DATA1234567"  # 11 bytes, far short of the 36-byte header
        self.sock_a.sendto(short_data, self.server_addr)

        self.sock_b.settimeout(0.3)
        with self.assertRaises(socket.timeout):
            self.sock_b.recvfrom(4096)

    def test_rejects_non_hex_gateway_id(self) -> None:
        frame = load_corpus_frame("f001")
        header = bytearray(cli.build_data(0x11111111, "NODE-A", frame)[:36])
        header[4:12] = b"ZZZZZZZZ"  # gateway_id is no longer hex
        bad = bytes(header) + frame
        self.sock_a.sendto(bad, self.server_addr)

        self.sock_b.settimeout(0.3)
        with self.assertRaises(socket.timeout):
            self.sock_b.recvfrom(4096)


# ---------------------------------------------------------------------------
# 4. MAX_ZEROS corrupt-datagram filter
# ---------------------------------------------------------------------------


class TestMaxZeros(ServerTestCase):
    def test_zero_run_helper_direct(self) -> None:
        self.assertFalse(srv._has_excess_zero_run(b"A" * 10 + b"\x00" * 6 + b"B"))
        self.assertTrue(srv._has_excess_zero_run(b"A" * 10 + b"\x00" * 7 + b"B"))

    def test_datagram_with_excess_zero_run_is_dropped_without_crash(self) -> None:
        sock = free_udp_socket()
        self.addCleanup(sock.close)

        good_keep = cli.build_keep(0x11111111, "NODE-A")
        corrupt = good_keep[:4] + b"\x00" * 7 + good_keep[4:]
        sock.sendto(corrupt, self.server_addr)

        sock.settimeout(0.3)
        with self.assertRaises(socket.timeout):
            sock.recvfrom(4096)  # no BEAT: the corrupt datagram was dropped

        # The server must still be alive and answer a legitimate KEEP.
        sock.settimeout(2.0)
        sock.sendto(good_keep, self.server_addr)
        beat, _addr = sock.recvfrom(4096)
        self.assertTrue(beat.startswith(b"BEAT"))


# ---------------------------------------------------------------------------
# 5. CONF builder, byte-by-byte
# ---------------------------------------------------------------------------


class TestConfBuilder(ServerTestCase):
    def test_build_conf_datagram_byte_exact(self) -> None:
        datagram = srv.build_conf_datagram("DK5EN-90", "DK5", 484024300, -114434000, 657)

        call_b = b"DK5EN-90"
        short_b = b"DK5"
        expected = bytearray(b"CONF")
        expected += b"\x00" + bytes([len(call_b)]) + call_b
        expected += b"\x01" + bytes([len(short_b)]) + short_b
        expected += b"\x02" + struct.pack("<i", 484024300)
        expected += b"\x03" + struct.pack("<i", -114434000)
        expected += b"\x04" + struct.pack("<i", 657)

        self.assertEqual(datagram, bytes(expected))

    def test_send_conf_delivers_same_bytes(self) -> None:
        sock = free_udp_socket()
        self.addCleanup(sock.close)
        addr = sock.getsockname()

        sent = self.server.send_conf(addr, "DK5EN-90", "DK5", 1, -1, 100)
        received, _addr = sock.recvfrom(4096)

        self.assertEqual(received, sent)
        self.assertTrue(received.startswith(b"CONF"))


# ---------------------------------------------------------------------------
# 6. KEEP parsing edge case: callsign shorter than 9 chars, space-padded
# ---------------------------------------------------------------------------


class TestKeepParsingEdgeCases(unittest.TestCase):
    def test_short_callsign_space_padded(self) -> None:
        keep = cli.build_keep(0x48A4690D, "DK5EN-9", groups=["232"])

        # The wire form must be space-padded to exactly 9 bytes (7 chars + 2 spaces).
        self.assertEqual(keep[12:21], b"DK5EN-9  ")

        fields = srv.parse_keep(keep)
        self.assertEqual(fields["gateway_id"], "48A4690D")
        self.assertEqual(fields["callsign"], "DK5EN-9")
        self.assertEqual(fields["version"], "4.35")
        self.assertEqual(fields["sub"], "p")
        self.assertEqual(fields["groups"], ["232"])


if __name__ == "__main__":
    unittest.main()
