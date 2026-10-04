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

    def test_accepts_hand_written_header_literal_via_raw_socket(self) -> None:
        """Finding 5: every other DATA test builds its datagram through
        mock_client.build_data() and lets the server parse it -- a shared
        encoder/decoder bug (e.g. a wrong field width both sides agree on)
        could drift together and never be caught. This test bypasses
        build_data() entirely: the 36-byte header is hand-assembled here
        and sent via a plain socket.sendto(), so only the server's parser
        (parse_data_header / DATA_HEADER_LEN) is exercised.

        doc 11 sec 2.1 layout, widths 4+8+9+4+1+4+4+2 = 36:
          "DATA" + %08X gw_id + %-9.9s callsign + %-4.4s version + %-1.1s sub
                 + %4i rssi + %4i snr + 2 ASCII modulation digits
        """
        header = (
            b"DATA"  # 4B  indicator
            b"1A2B3C4D"  # 8B  %08X gateway_id
            b"OE0XXX-1 "  # 9B  %-9.9s callsign: "OE0XXX-1" (8) + 1 pad space
            b"4.35"  # 4B  %-4.4s version
            b"p"  # 1B  %-1.1s sub
            b" -80"  # 4B  %4i rssi = -80  (" -80")
            b"   7"  # 4B  %4i snr  =   7  ("   7")
            b"03"  # 2B  2 ASCII modulation digits
        )
        self.assertEqual(len(header), srv.DATA_HEADER_LEN)

        frame = load_corpus_frame("f001")
        self.sock_a.sendto(header + frame, self.server_addr)  # raw socket, not cli.build_data

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


class TestInjectionCallsignGuard(ServerTestCase):
    """send_gate() must never put a foreign callsign on the air: the gateway
    radiates injected frames verbatim, so only our own DK5EN-* may appear in
    the source path. Relayed DATA frames from a registered gateway are genuine
    on-air traffic and stay exempt."""

    @staticmethod
    def frame_with_path(path: bytes) -> bytes:
        body = path + b">*:{CET}bench\x00\x00\x88"
        return bytes([0x3A, 1, 2, 3, 4, 0xB0]) + body + b"\x00\x00" + bytes.fromhex("00AB237E")

    def test_source_path_of_splits_elements(self) -> None:
        fr = self.frame_with_path(b"DK5EN-93,DK5EN-91")
        self.assertEqual(srv.source_path_of(fr), ["DK5EN-93", "DK5EN-91"])

    def test_own_callsign_frame_is_sent(self) -> None:
        node_sock = free_udp_socket()
        self.addCleanup(node_sock.close)
        node_sock.settimeout(2.0)
        fr = self.frame_with_path(b"DK5EN-93")
        sent = self.server.send_gate(fr, node_sock.getsockname())
        data, _ = node_sock.recvfrom(4096)
        self.assertEqual(data, sent)
        self.assertEqual(data, b"GATE" + fr)

    def test_foreign_callsign_frame_is_refused_and_not_sent(self) -> None:
        node_sock = free_udp_socket()
        self.addCleanup(node_sock.close)
        node_sock.settimeout(0.3)
        # a real third-party station in the relay path, our own call in front
        fr = self.frame_with_path(b"DK5EN-93,DL2JA-2")
        with self.assertRaises(srv.ForeignCallsignError):
            self.server.send_gate(fr, node_sock.getsockname())
        with self.assertRaises(socket.timeout):
            node_sock.recvfrom(4096)

    def test_foreign_source_callsign_is_refused(self) -> None:
        with self.assertRaises(srv.ForeignCallsignError):
            srv.assert_own_source_path(self.frame_with_path(b"DL2JA-1,DL2JA-2"))

    def test_frame_without_path_is_refused(self) -> None:
        with self.assertRaises(srv.ForeignCallsignError):
            srv.assert_own_source_path(bytes([0x3A, 1, 2, 3, 4, 0xB0]) + b"no path here")

    def test_relayed_frame_skips_the_guard(self) -> None:
        node_sock = free_udp_socket()
        self.addCleanup(node_sock.close)
        node_sock.settimeout(2.0)
        fr = self.frame_with_path(b"DL2JA-1,DL2JA-2")
        self.server.send_gate(fr, node_sock.getsockname(), relayed=True)
        data, _ = node_sock.recvfrom(4096)
        self.assertEqual(data, b"GATE" + fr)


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

    def test_send_conf_matches_hand_written_literal(self) -> None:
        """Finding 5: the old version of this test only compared the
        received bytes against send_conf()'s own return value -- a
        tautological round-trip of the same bytes object over loopback that
        can never fail even if build_conf_datagram() and the server both
        drift together. This version derives the expected datagram by hand
        from doc 11 sec 2.2's TLV layout and checks BOTH the builder output
        and the bytes actually placed on the wire against it.

        Field values: callsign="OE0XXX-1" (8B), shortname="XX1" (3B),
        lat=473512340 (positive), lon=-164712345 (negative, exercises two's
        complement), alt=1234.

        int32 LE encoding worked out by hand:
          lat   473512340 = 0x1c393994 (BE) -> LE bytes 94 39 39 1c
          lon  -164712345 -> unsigned32 = 2**32 + lon = 4130254951
                            = 0xf62eb067 (BE) -> LE bytes 67 b0 2e f6
          alt         1234 = 0x000004d2 (BE) -> LE bytes d2 04 00 00
        """
        literal = (
            b"CONF"
            + b"\x00\x08OE0XXX-1"  # 0x00, len=8, "OE0XXX-1"
            + b"\x01\x03XX1"  # 0x01, len=3, "XX1"
            + b"\x02\x94\x39\x39\x1c"  # 0x02, lat  473512340 LE
            + b"\x03\x67\xb0\x2e\xf6"  # 0x03, lon -164712345 LE
            + b"\x04\xd2\x04\x00\x00"  # 0x04, alt        1234 LE
        )

        # 1) the builder's output must equal the hand-written literal.
        built = srv.build_conf_datagram(
            "OE0XXX-1", "XX1", 473512340, -164712345, 1234
        )
        self.assertEqual(built, literal)

        # 2) the bytes actually sent over the wire by the server must equal
        #    it too -- this is what the tautological version never checked.
        sock = free_udp_socket()
        self.addCleanup(sock.close)
        addr = sock.getsockname()

        sent = self.server.send_conf(addr, "OE0XXX-1", "XX1", 473512340, -164712345, 1234)
        received, _addr = sock.recvfrom(4096)

        self.assertEqual(sent, literal)
        self.assertEqual(received, literal)


# ---------------------------------------------------------------------------
# 6. Registry expiry (_expire_clients / registry_ttl)
# ---------------------------------------------------------------------------


class TestRegistryExpiry(unittest.TestCase):
    def test_expired_client_removed_and_not_forwarded_to(self) -> None:
        server = srv.MockMeshComServer(
            "127.0.0.1", 0, callsign="MOCK-SRV", verbose=False, registry_ttl=0.2
        )
        server.start()
        self.addCleanup(server.stop)
        addr = ("127.0.0.1", server.port)

        sock_a = free_udp_socket()
        self.addCleanup(sock_a.close)
        sock_a.sendto(cli.build_keep(0x11111111, "NODE-A"), addr)
        sock_a.recvfrom(4096)  # BEAT
        wait_until(lambda: len(server.clients) == 1)

        # _expire_clients() runs lazily, at the top of _handle_datagram, on
        # every received packet -- there is no background timer. Poll past
        # the 0.2s TTL by sending harmless traffic (too short to be a valid
        # KEEP/DATA, but still long enough to run the expiry sweep) until
        # the registry (the documented accessor: server.clients) empties,
        # capped at ~1s.
        def a_expired() -> bool:
            sock_a.sendto(b"\x00", addr)
            return len(server.clients) == 0

        self.assertTrue(wait_until(a_expired, timeout=1.0, interval=0.05))
        self.assertEqual(len(server.clients), 0)

        # Register a fresh client B and confirm the (now-expired) A gets
        # nothing when B sends DATA -- the registry no longer has an entry
        # to forward it to.
        sock_b = free_udp_socket()
        self.addCleanup(sock_b.close)
        sock_b.sendto(cli.build_keep(0x22222222, "NODE-B"), addr)
        sock_b.recvfrom(4096)  # BEAT
        wait_until(lambda: len(server.clients) == 1)

        frame = load_corpus_frame("f001")
        sock_b.sendto(cli.build_data(0x22222222, "NODE-B", frame), addr)

        sock_a.settimeout(0.3)
        with self.assertRaises(socket.timeout):
            sock_a.recvfrom(4096)


# ---------------------------------------------------------------------------
# 7. KEEP parsing edge case: callsign shorter than 9 chars, space-padded
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


# ---------------------------------------------------------------------------
# 9. STOR (docs/snf-gateway-concept-20261002.md section 5.3)
# ---------------------------------------------------------------------------


def build_stor(
    gwid: int,
    call: str,
    calls: list[str],
    *,
    seq: int = 1,
    total: int = 1,
    hold_h: int = 24,
    ver: str = "4.40",
    sub: str = "a",
) -> bytes:
    body = (
        f"STOR{gwid:08X}{call:<9.9}{ver:<4.4}{sub:<1.1}"
        f"{hold_h};{seq}/{total};" + "".join(f"{c};" for c in calls)
    )
    return body.encode("ascii") + b"\x00"


def text_frame(dest: str, src: str = "DK5EN-93") -> bytes:
    body = f"{src}>{dest}:hello".encode("ascii")
    return bytes([0x3A, 1, 2, 3, 4, 0xB0]) + body + b"\x00\x00" + bytes.fromhex("00AB237E")


class TestParseStor(unittest.TestCase):
    def test_valid_single_chunk_matches_concept_example(self) -> None:
        raw = b"STOR1A2B3C4DDK5EN-90 4.40a24;1/1;DK5EN-92;DK5EN-93;\x00"
        self.assertEqual(
            srv.parse_stor(raw),
            {
                "gwid": 0x1A2B3C4D,
                "call": "DK5EN-90",
                "ver": "4.40",
                "sub": "a",
                "hold_h": 24,
                "seq": 1,
                "total": 1,
                "calls": ["DK5EN-92", "DK5EN-93"],
            },
        )
        self.assertEqual(srv.parse_stor(raw), srv.parse_stor(raw[:-1]))  # NUL optional

    def test_builder_matches_concept_example_bytes(self) -> None:
        self.assertEqual(
            build_stor(0x1A2B3C4D, "DK5EN-90", ["DK5EN-92", "DK5EN-93"]),
            b"STOR1A2B3C4DDK5EN-90 4.40a24;1/1;DK5EN-92;DK5EN-93;\x00",
        )

    def test_multi_chunk_header_fields(self) -> None:
        f = srv.parse_stor(build_stor(1, "DK5EN-90", ["A-1"], seq=2, total=3, hold_h=6))
        self.assertEqual((f["seq"], f["total"], f["hold_h"], f["calls"]), (2, 3, 6, ["A-1"]))

    def test_empty_list_is_a_withdrawal(self) -> None:
        f = srv.parse_stor(b"STOR1A2B3C4DDK5EN-90 4.40a24;1/1;\x00")
        self.assertEqual(f["calls"], [])
        self.assertEqual((f["seq"], f["total"]), (1, 1))

    def test_max_length_255_accepted_256_rejected(self) -> None:
        calls = ["AAAAAAAAA"] * 22  # 22 * 10 B + 34 B header ("240;1/1;") = 254 + NUL
        ok = build_stor(1, "DK5EN-90", calls, hold_h=240)
        self.assertEqual(len(ok), 255)
        self.assertEqual(len(srv.parse_stor(ok)["calls"]), 22)
        too_long = build_stor(1, "DK5EN-90", calls, hold_h=2400)
        self.assertEqual(len(too_long), 256)
        with self.assertRaisesRegex(srv.StorParseError, "too long"):
            srv.parse_stor(too_long)

    def test_malformed_cases(self) -> None:
        good = b"STOR1A2B3C4DDK5EN-90 4.40a24;1/1;DK5EN-92;\x00"
        cases = {
            "bad prefix": b"KEEP" + good[4:],
            "short header": b"STOR1A2B3C4DDK5EN\x00",
            "non-hex gwid": b"STOR1A2B3CZZ" + good[12:],
            "non-ascii": good.replace(b"DK5EN-92", b"DK5\xc3\xa4N-92"),
            "missing trailing semicolon": b"STOR1A2B3C4DDK5EN-90 4.40a24;1/1;DK5EN-92\x00",
            "no seq field": b"STOR1A2B3C4DDK5EN-90 4.40a24;\x00",
            "hold_h not numeric": b"STOR1A2B3C4DDK5EN-90 4.40ax4;1/1;A;\x00",
            "hold_h empty": b"STOR1A2B3C4DDK5EN-90 4.40a;1/1;A;\x00",
            "hold_h negative": b"STOR1A2B3C4DDK5EN-90 4.40a-1;1/1;A;\x00",
            "seq no slash": b"STOR1A2B3C4DDK5EN-90 4.40a24;1-1;A;\x00",
            "seq zero": b"STOR1A2B3C4DDK5EN-90 4.40a24;0/1;A;\x00",
            "seq above total": b"STOR1A2B3C4DDK5EN-90 4.40a24;3/2;A;\x00",
            "total zero": b"STOR1A2B3C4DDK5EN-90 4.40a24;0/0;A;\x00",
            "empty call in list": b"STOR1A2B3C4DDK5EN-90 4.40a24;1/1;A;;B;\x00",
            "bad call char": b"STOR1A2B3C4DDK5EN-90 4.40a24;1/1;A B;\x00",
            "call too long": b"STOR1A2B3C4DDK5EN-90 4.40a24;1/1;ABCDEFGHIJ;\x00",
            "embedded NUL": b"STOR1A2B3C4DDK5EN-90 4.40a24;1/1;A;\x00B;\x00",
            "too long": good[:-1] + b"A;" * 130 + b"\x00",
            "blank gateway call": b"STOR1A2B3C4D         4.40a24;1/1;A;\x00",
        }
        for name, raw in cases.items():
            with self.subTest(name), self.assertRaises(srv.StorParseError):
                srv.parse_stor(raw)

    def test_dest_call_of(self) -> None:
        self.assertEqual(srv.dest_call_of(text_frame("DK5EN-92")), "DK5EN-92")
        self.assertIsNone(srv.dest_call_of(b"\x21" + text_frame("X")[1:]))  # not a text frame
        self.assertIsNone(srv.dest_call_of(bytes([0x3A, 1, 2, 3, 4, 0xB0]) + b"no path"))


class StorServerTestCase(ServerTestCase):
    """ServerTestCase plus a fake store gateway socket and a STOR helper."""

    def stor(self, sock: socket.socket, call: str, calls: list[str], **kw) -> None:
        sock.sendto(build_stor(0x1A2B3C4D, call, calls, **kw), self.server_addr)

    def snapshot_for(self, call: str):
        wait_until(lambda: call in self.server.stor_snapshots, timeout=0.5)
        return self.server.stor_snapshots.get(call)

    def settle(self) -> None:
        """Round-trip a KEEP so every earlier datagram has been handled (the
        server thread is sequential and answers each KEEP)."""
        probe = free_udp_socket()
        self.addCleanup(probe.close)
        probe.sendto(cli.build_keep(0x0F0F0F0F, "PROBE"), self.server_addr)
        probe.recvfrom(4096)


class TestStorAssembly(StorServerTestCase):
    def test_single_chunk_snapshot_stored_and_not_answered(self) -> None:
        sock = free_udp_socket()
        self.addCleanup(sock.close)
        self.stor(sock, "DK5EN-90", ["DK5EN-92", "DK5EN-93"])
        snap = self.snapshot_for("DK5EN-90")
        self.assertEqual(snap["calls"], ["DK5EN-92", "DK5EN-93"])
        self.assertEqual(snap["hold_h"], 24)
        self.assertEqual(snap["addr"], sock.getsockname())
        self.assertIsInstance(snap["t"], float)
        sock.settimeout(0.3)
        with self.assertRaises(socket.timeout):  # the mock never answers STOR
            sock.recvfrom(4096)

    def test_stor_does_not_register_a_client(self) -> None:
        sock = free_udp_socket()
        self.addCleanup(sock.close)
        self.stor(sock, "DK5EN-90", ["A-1"])
        self.snapshot_for("DK5EN-90")
        self.assertEqual(len(self.server.clients), 0)

    def test_multi_chunk_in_order_is_applied_when_complete(self) -> None:
        sock = free_udp_socket()
        self.addCleanup(sock.close)
        self.stor(sock, "DK5EN-90", ["A-1", "A-2"], seq=1, total=3)
        self.stor(sock, "DK5EN-90", ["A-3"], seq=2, total=3)
        self.settle()
        self.assertNotIn("DK5EN-90", self.server.stor_snapshots)  # 2 of 3
        self.stor(sock, "DK5EN-90", ["A-4"], seq=3, total=3)
        snap = self.snapshot_for("DK5EN-90")
        self.assertEqual(snap["calls"], ["A-1", "A-2", "A-3", "A-4"])

    def test_chunks_two_and_three_may_arrive_out_of_order(self) -> None:
        sock = free_udp_socket()
        self.addCleanup(sock.close)
        self.stor(sock, "DK5EN-90", ["A-1"], seq=1, total=3)
        self.stor(sock, "DK5EN-90", ["A-3"], seq=3, total=3)
        self.stor(sock, "DK5EN-90", ["A-2"], seq=2, total=3)
        snap = self.snapshot_for("DK5EN-90")
        self.assertEqual(snap["calls"], ["A-1", "A-2", "A-3"])  # seq order, not arrival

    def test_round_with_missing_chunk_is_not_applied(self) -> None:
        sock = free_udp_socket()
        self.addCleanup(sock.close)
        self.stor(sock, "DK5EN-90", ["A-1"], seq=1, total=3)
        self.stor(sock, "DK5EN-90", ["A-3"], seq=3, total=3)
        self.settle()
        self.assertEqual(self.server.stor_snapshots, {})

    def test_missing_chunk_keeps_previous_snapshot(self) -> None:
        sock = free_udp_socket()
        self.addCleanup(sock.close)
        self.stor(sock, "DK5EN-90", ["OLD-1"])
        self.snapshot_for("DK5EN-90")
        self.stor(sock, "DK5EN-90", ["N-1"], seq=1, total=2)  # 2/2 never comes
        self.settle()
        self.assertEqual(self.server.stor_snapshots["DK5EN-90"]["calls"], ["OLD-1"])

    def test_chunk_without_round_is_dropped(self) -> None:
        sock = free_udp_socket()
        self.addCleanup(sock.close)
        self.stor(sock, "DK5EN-90", ["A-2"], seq=2, total=2)  # 1/2 not seen yet
        self.stor(sock, "DK5EN-90", ["A-1"], seq=1, total=2)  # starts a round, no 2/2
        self.settle()
        self.assertEqual(self.server.stor_snapshots, {})

    def test_seq1_starts_new_round_and_old_round_stragglers_are_dropped(self) -> None:
        sock = free_udp_socket()
        self.addCleanup(sock.close)
        self.stor(sock, "DK5EN-90", ["OLD-1"], seq=1, total=2)
        self.stor(sock, "DK5EN-90", ["NEW-1"], seq=1, total=2)  # new round
        self.stor(sock, "DK5EN-90", ["NEW-2"], seq=2, total=2)
        snap = self.snapshot_for("DK5EN-90")
        self.assertEqual(snap["calls"], ["NEW-1", "NEW-2"])
        # a late chunk 2/2 of the older round: no round in progress -> dropped
        self.stor(sock, "DK5EN-90", ["OLD-2"], seq=2, total=2)
        self.settle()
        self.assertEqual(self.server.stor_snapshots["DK5EN-90"]["calls"], ["NEW-1", "NEW-2"])

    def test_duplicate_chunk_in_open_round_is_dropped(self) -> None:
        sock = free_udp_socket()
        self.addCleanup(sock.close)
        self.stor(sock, "DK5EN-90", ["A-1"], seq=1, total=3)
        self.stor(sock, "DK5EN-90", ["A-2"], seq=2, total=3)
        self.stor(sock, "DK5EN-90", ["STALE"], seq=2, total=3)  # older-round straggler
        self.stor(sock, "DK5EN-90", ["A-3"], seq=3, total=3)
        snap = self.snapshot_for("DK5EN-90")
        self.assertEqual(snap["calls"], ["A-1", "A-2", "A-3"])

    def test_new_snapshot_replaces_old_and_empty_list_withdraws(self) -> None:
        sock = free_udp_socket()
        self.addCleanup(sock.close)
        self.stor(sock, "DK5EN-90", ["A-1", "A-2"])
        self.snapshot_for("DK5EN-90")
        self.stor(sock, "DK5EN-90", [])
        self.assertTrue(
            wait_until(lambda: self.server.stor_snapshots["DK5EN-90"]["calls"] == [])
        )

    def test_malformed_stor_is_dropped_without_crash(self) -> None:
        sock = free_udp_socket()
        self.addCleanup(sock.close)
        sock.sendto(b"STOR1A2B3C4DDK5EN-90 4.40a24;9/1;A;\x00", self.server_addr)
        self.settle()
        self.assertEqual(self.server.stor_snapshots, {})
        self.stor(sock, "DK5EN-90", ["A-1"])  # server still alive
        self.assertIsNotNone(self.snapshot_for("DK5EN-90"))

    def test_stor_is_recorded(self) -> None:
        rec = srv.DatagramRecorder()
        server = srv.MockMeshComServer("127.0.0.1", 0, recorder=rec)
        server.start()
        self.addCleanup(server.stop)
        sock = free_udp_socket()
        self.addCleanup(sock.close)
        raw = build_stor(1, "DK5EN-90", ["A-1"])
        sock.sendto(raw, ("127.0.0.1", server.port))
        self.assertTrue(wait_until(lambda: "DK5EN-90" in server.stor_snapshots))
        self.assertEqual([(r.direction, r.data) for r in rec.records], [("rx", raw)])
        self.assertEqual(rec.records[0].indicator, "STOR")


class TestStorExpiry(unittest.TestCase):
    def test_snapshot_expires_after_ttl(self) -> None:
        server = srv.MockMeshComServer("127.0.0.1", 0, stor_ttl=0.2)
        server.start()
        self.addCleanup(server.stop)
        addr = ("127.0.0.1", server.port)
        sock = free_udp_socket()
        self.addCleanup(sock.close)
        sock.sendto(build_stor(1, "DK5EN-90", ["A-1"]), addr)
        self.assertTrue(wait_until(lambda: "DK5EN-90" in server.stor_snapshots))

        # Expiry is lazy (top of _handle_datagram), like the client registry.
        def expired() -> bool:
            sock.sendto(b"\x00", addr)
            return "DK5EN-90" not in server.stor_snapshots

        self.assertTrue(wait_until(expired, timeout=1.5, interval=0.05))

    def test_default_ttl_is_45_minutes_and_refresh_resets_it(self) -> None:
        server = srv.MockMeshComServer("127.0.0.1", 0)
        self.addCleanup(server.sock.close)
        self.assertEqual(server.stor_ttl, 45 * 60)
        addr = ("127.0.0.1", 40000)
        server._handle_datagram(build_stor(1, "DK5EN-90", ["A-1"]), addr)
        server.stor_snapshots["DK5EN-90"]["t"] -= 44 * 60  # nearly stale
        server._handle_datagram(b"\x00", addr)
        self.assertIn("DK5EN-90", server.stor_snapshots)
        server._handle_datagram(build_stor(1, "DK5EN-90", ["A-1"]), addr)  # refresh
        server.stor_snapshots["DK5EN-90"]["t"] -= 44 * 60
        server._handle_datagram(b"\x00", addr)
        self.assertIn("DK5EN-90", server.stor_snapshots)
        server.stor_snapshots["DK5EN-90"]["t"] -= 2 * 60  # now 46 min old
        server._handle_datagram(b"\x00", addr)
        self.assertNotIn("DK5EN-90", server.stor_snapshots)


class TestStorRouting(unittest.TestCase):
    """Routing is a mock assumption and OFF by default."""

    def make(self, **kw):
        server = srv.MockMeshComServer("127.0.0.1", 0, registry_ttl=120.0, **kw)
        server.start()
        self.addCleanup(server.stop)
        addr = ("127.0.0.1", server.port)
        socks = {}
        for name, gw in (("src", 0x11111111), ("store", 0x22222222), ("other", 0x33333333)):
            s = free_udp_socket(0.3)
            self.addCleanup(s.close)
            s.sendto(cli.build_keep(gw, name.upper()), addr)
            s.recvfrom(4096)
            socks[name] = s
        socks["store"].sendto(build_stor(0x22222222, "STORE", ["DK5EN-92"]), addr)
        self.assertTrue(wait_until(lambda: "STORE" in server.stor_snapshots))
        return server, addr, socks

    @staticmethod
    def got(sock: socket.socket) -> bytes | None:
        try:
            return sock.recvfrom(4096)[0]
        except socket.timeout:
            return None

    def test_default_off_routes_nothing_extra(self) -> None:
        server, addr, s = self.make(redistribute=False)
        self.assertFalse(server.stor_routing)
        s["src"].sendto(cli.build_data(0x11111111, "SRC", text_frame("DK5EN-92")), addr)
        self.assertIsNone(self.got(s["store"]))
        self.assertIsNone(self.got(s["other"]))

    def test_on_forwards_to_listing_gateway_only_when_redistribute_off(self) -> None:
        server, addr, s = self.make(redistribute=False, stor_routing=True)
        fr = text_frame("DK5EN-92")
        s["src"].sendto(cli.build_data(0x11111111, "SRC", fr), addr)
        self.assertEqual(self.got(s["store"]), b"GATE" + fr)
        self.assertIsNone(self.got(s["other"]))
        self.assertIsNone(self.got(s["src"]))

    def test_on_is_in_addition_to_redistribution_without_duplicates(self) -> None:
        server, addr, s = self.make(stor_routing=True)
        fr = text_frame("DK5EN-92")
        s["src"].sendto(cli.build_data(0x11111111, "SRC", fr), addr)
        self.assertEqual(self.got(s["store"]), b"GATE" + fr)
        self.assertEqual(self.got(s["other"]), b"GATE" + fr)  # redistribution kept
        self.assertIsNone(self.got(s["store"]))  # exactly one copy
        self.assertIsNone(self.got(s["src"]))

    def test_unlisted_destination_is_not_routed(self) -> None:
        server, addr, s = self.make(redistribute=False, stor_routing=True)
        s["src"].sendto(cli.build_data(0x11111111, "SRC", text_frame("DK5EN-77")), addr)
        self.assertIsNone(self.got(s["store"]))

    def test_non_text_frame_is_not_routed(self) -> None:
        server, addr, s = self.make(redistribute=False, stor_routing=True)
        fr = b"\x21" + text_frame("DK5EN-92")[1:]
        s["src"].sendto(cli.build_data(0x11111111, "SRC", fr), addr)
        self.assertIsNone(self.got(s["store"]))

    def test_expired_snapshot_is_not_routed_to(self) -> None:
        server, addr, s = self.make(redistribute=False, stor_routing=True)
        server.stor_snapshots["STORE"]["t"] -= 46 * 60
        s["src"].sendto(cli.build_data(0x11111111, "SRC", text_frame("DK5EN-92")), addr)
        self.assertIsNone(self.got(s["store"]))

    def test_withdrawn_snapshot_is_not_routed_to(self) -> None:
        server, addr, s = self.make(redistribute=False, stor_routing=True)
        s["store"].sendto(build_stor(0x22222222, "STORE", []), addr)
        self.assertTrue(wait_until(lambda: server.stor_snapshots["STORE"]["calls"] == []))
        s["src"].sendto(cli.build_data(0x11111111, "SRC", text_frame("DK5EN-92")), addr)
        self.assertIsNone(self.got(s["store"]))

    def test_sender_never_gets_its_own_frame_back(self) -> None:
        server, addr, s = self.make(redistribute=False, stor_routing=True)
        fr = text_frame("DK5EN-92")
        s["store"].sendto(cli.build_data(0x22222222, "STORE", fr), addr)
        self.assertIsNone(self.got(s["store"]))

    def test_routed_foreign_path_frame_is_relayed_not_guarded(self) -> None:
        """Genuine DATA from a registered gateway keeps relayed=True: a foreign
        source callsign in its path must still be forwarded, as for redistribute."""
        server, addr, s = self.make(redistribute=False, stor_routing=True)
        fr = text_frame("DK5EN-92", src="DL2JA-1")
        s["src"].sendto(cli.build_data(0x11111111, "SRC", fr), addr)
        self.assertEqual(self.got(s["store"]), b"GATE" + fr)

    def test_injection_guard_unchanged_by_routing(self) -> None:
        server, addr, s = self.make(stor_routing=True)
        with self.assertRaises(srv.ForeignCallsignError):
            server.send_gate(text_frame("DK5EN-92", src="DL2JA-1"), s["store"].getsockname())


if __name__ == "__main__":
    unittest.main()


# ---------------------------------------------------------------------------
# 8. Capture recording, corpus replay and the deterministic capture mode
#    (test plan P0.6, steps H6/H7)
# ---------------------------------------------------------------------------


class TestRecorder(unittest.TestCase):
    def test_records_both_directions_in_order(self) -> None:
        recorder = srv.DatagramRecorder()
        server = srv.MockMeshComServer(
            "127.0.0.1", 0, recorder=recorder, registry_ttl=120.0
        )
        server.start()
        self.addCleanup(server.stop)

        node_sock = free_udp_socket()
        self.addCleanup(node_sock.close)
        keep = cli.build_keep(0x48A4690D, "DK5EN-90")
        node_sock.sendto(keep, ("127.0.0.1", server.port))
        beat, _ = node_sock.recvfrom(4096)

        wait_until(lambda: len(recorder.records) == 2)
        self.assertEqual([r.direction for r in recorder.records], ["rx", "tx"])
        self.assertEqual(recorder.records[0].data, keep)
        self.assertEqual(recorder.records[1].data, beat)
        self.assertEqual(recorder.records[0].indicator, "KEEP")
        self.assertEqual(recorder.records[1].indicator, "BEAT")
        # Monotonic and non-decreasing, so the text log reads as a sequence.
        self.assertLessEqual(recorder.records[0].t, recorder.records[1].t)

    def test_binary_is_length_prefixed_in_node_direction(self) -> None:
        recorder = srv.DatagramRecorder()
        recorder.record("rx", ("127.0.0.1", 1), b"KEEPxx")
        recorder.record("tx", ("127.0.0.1", 1), b"BEAT")
        recorder.record("rx", ("127.0.0.1", 1), b"DATAyyy")

        # "rx" here is what the node transmitted.
        self.assertEqual(
            recorder.binary("rx"),
            struct.pack("<H", 6) + b"KEEPxx" + struct.pack("<H", 7) + b"DATAyyy",
        )
        self.assertEqual(recorder.binary("tx"), struct.pack("<H", 4) + b"BEAT")

    def test_write_produces_the_three_capture_files(self) -> None:
        import tempfile

        recorder = srv.DatagramRecorder()
        recorder.record("rx", ("10.0.0.9", 1990), b"KEEP1234")
        recorder.record("tx", ("10.0.0.9", 1990), b"BEAT")

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "G0" / "rak-90"
            written = recorder.write(out)
            self.assertEqual(
                sorted(p.name for p in written),
                ["udp-log.txt", "udp-rx.bin", "udp-tx.bin"],
            )
            log = (out / "udp-log.txt").read_text()
            self.assertIn("rx 10.0.0.9:1990 ind=KEEP len=8 4b45455031323334", log)
            self.assertIn("tx 10.0.0.9:1990 ind=BEAT len=4", log)
            # udp-tx.bin is the node's transmissions, i.e. our rx.
            self.assertEqual(
                (out / "udp-tx.bin").read_bytes(),
                struct.pack("<H", 8) + b"KEEP1234",
            )


class TestCorpusLoader(unittest.TestCase):
    def test_reads_bin_and_hex_in_sorted_order(self) -> None:
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            (d / "02-beat.bin").write_bytes(b"BEAT")
            (d / "01-gate.hex").write_text("# a GATE frame\n4741 5445 21\n")
            (d / "notes.md").write_text("ignored")
            self.assertEqual(srv.load_corpus(d), [b"GATE!", b"BEAT"])


class TestReplay(ServerTestCase):
    def test_replay_sends_verbatim_in_order_and_repeats(self) -> None:
        node_sock = free_udp_socket()
        self.addCleanup(node_sock.close)
        self.register(node_sock, 0x48A4690D, "DK5EN-90")
        wait_until(lambda: len(self.server.clients) == 1)

        addr = self.server.wait_for_client(timeout=1.0)
        self.assertIsNotNone(addr)

        corpus = [b"GATE\x01", b"GATE\x02"]
        sent = self.server.replay(corpus, addr, gap=0.0, repeat=2)
        self.assertEqual(sent, 4)

        got = [node_sock.recvfrom(4096)[0] for _ in range(4)]
        self.assertEqual(got, corpus + corpus)

    def test_wait_for_client_times_out_without_keep(self) -> None:
        self.assertIsNone(self.server.wait_for_client(timeout=0.2))


class TestNoRedistribute(unittest.TestCase):
    def test_data_is_not_forwarded_when_disabled(self) -> None:
        server = srv.MockMeshComServer(
            "127.0.0.1", 0, redistribute=False, registry_ttl=120.0
        )
        server.start()
        self.addCleanup(server.stop)
        addr = ("127.0.0.1", server.port)

        sock_a = free_udp_socket()
        sock_b = free_udp_socket()
        self.addCleanup(sock_a.close)
        self.addCleanup(sock_b.close)
        for sock, gw, call in ((sock_a, 0x11111111, "NODE-A"), (sock_b, 0x22222222, "NODE-B")):
            sock.sendto(cli.build_keep(gw, call), addr)
            sock.recvfrom(4096)
        wait_until(lambda: len(server.clients) == 2)

        sock_a.sendto(cli.build_data(0x11111111, "NODE-A", load_corpus_frame("f001")), addr)

        sock_b.settimeout(0.3)
        with self.assertRaises(socket.timeout):
            sock_b.recvfrom(4096)
