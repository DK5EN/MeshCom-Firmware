#!/usr/bin/env python3
"""Regression tests for extudp_peer.py (TM-43).

    python3 -m unittest tools/bench/test_extudp_peer.py

No hardware and no node: the peer is bound to 127.0.0.1 on an ephemeral port
and driven against a second loopback socket that plays the node -- which is
enough to pin everything the harness relies on (timestamped receive thread,
byte-exact send, JSON parsing, heartbeat sequence gaps, the rejection vectors).
"""

import os
import socket
import sys
import time
import unittest
from typing import List, Optional, Tuple

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import extudp_peer as ep  # noqa: E402


def _loopback_peer() -> ep.ExtUdpPeer:
    """A peer on 127.0.0.1 with a kernel-assigned port (1799 may be in use)."""
    return ep.ExtUdpPeer(bind_host="127.0.0.1", bind_port=0).start()


class FakeNode:
    """The other end of the loopback link: sends to the peer, receives from it."""

    def __init__(self) -> None:
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.settimeout(2.0)

    @property
    def ip(self) -> str:
        return str(self.sock.getsockname()[0])

    @property
    def port(self) -> int:
        return int(self.sock.getsockname()[1])

    def send(self, data: bytes, peer: ep.ExtUdpPeer) -> None:
        self.sock.sendto(data, ("127.0.0.1", peer.bind_port))

    def recv(self) -> Optional[bytes]:
        try:
            return self.sock.recvfrom(4096)[0]
        except (socket.timeout, TimeoutError):
            return None

    def close(self) -> None:
        self.sock.close()


def _wait_count(peer: ep.ExtUdpPeer, n: int, timeout: float = 2.0) -> List[ep.Datagram]:
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        got = peer.since(0)
        if len(got) >= n:
            return got
        time.sleep(0.02)
    return peer.since(0)


# ------------------------------------------------------------ pure helpers


class TestParsing(unittest.TestCase):
    def test_parse_json_object(self) -> None:
        obj = ep.parse_json(b'{"src_type":"node","type":"pos","lat":48.1}')
        assert obj is not None
        self.assertEqual(obj["src_type"], "node")
        self.assertAlmostEqual(obj["lat"], 48.1)

    def test_parse_json_rejects_non_object_and_garbage(self) -> None:
        # Every documented EXTUDP datagram is a top-level object; a bare list,
        # a bare number or truncated JSON must come back as None, not raise.
        for raw in (b"[1,2,3]", b"42", b'{"type":"msg",', b"", b"\xff\xfe\x00rubbish"):
            self.assertIsNone(ep.parse_json(raw), raw)

    def test_heartbeat_seq(self) -> None:
        self.assertEqual(ep.heartbeat_seq({"type": "hb", "seq": 7, "ms": 1234}), 7)
        self.assertIsNone(ep.heartbeat_seq({"type": "pos", "seq": 7}))
        self.assertIsNone(ep.heartbeat_seq({"type": "hb"}))
        self.assertIsNone(ep.heartbeat_seq({"type": "hb", "seq": "7"}))
        self.assertIsNone(ep.heartbeat_seq(None))
        # bool is an int in Python -- it must not pass as a sequence number
        self.assertIsNone(ep.heartbeat_seq({"type": "hb", "seq": True}))

    def test_seq_gaps(self) -> None:
        self.assertEqual(ep.seq_gaps([0, 1, 2, 3]), [])
        self.assertEqual(ep.seq_gaps([0, 1, 5, 6]), [(1, 5)])
        self.assertEqual(ep.seq_gaps([]), [])
        self.assertEqual(ep.seq_gaps([4]), [])
        # A node rebooting mid-run restarts the sequence: that is a gap, not
        # a smooth continuation -- it is exactly what TM-43 hunts for.
        self.assertEqual(ep.seq_gaps([8, 9, 0, 1]), [(9, 0)])

    def test_is_out_datagram(self) -> None:
        def dg(obj: dict) -> ep.Datagram:
            return ep.Datagram(t=0.0, addr=("1.2.3.4", 1799), raw=b"", obj=obj)

        self.assertTrue(ep.is_out_datagram(dg({"src_type": "node", "type": "pos"})))
        self.assertTrue(ep.is_out_datagram(dg({"src_type": "lora", "type": "msg"})))
        self.assertFalse(ep.is_out_datagram(dg({"type": "hb", "seq": 1})))
        self.assertFalse(ep.is_out_datagram(dg({"src_type": "node", "type": "notice"})))
        self.assertFalse(ep.is_out_datagram(ep.Datagram(0.0, ("1.2.3.4", 1799), b"x", None)))


class TestRejectionVectors(unittest.TestCase):
    def test_seven_vectors_with_unique_names(self) -> None:
        vecs = ep.rejection_vectors()
        self.assertEqual(len(vecs), 7)
        self.assertEqual(len({n for n, _ in vecs}), 7)

    def test_full_datagram_is_exactly_udp_tx_buf_size(self) -> None:
        # The node reads at most UDP_TX_BUF_SIZE-1 bytes and NUL-terminates;
        # this vector must sit exactly on that boundary to be worth sending.
        vec = dict(ep.rejection_vectors())["full_255_byte_datagram"]
        self.assertEqual(len(vec), ep.UDP_TX_BUF_SIZE)

    def test_length_vectors_sit_one_past_the_firmware_limits(self) -> None:
        vecs = dict(ep.rejection_vectors())
        self.assertIn(b'"dst":"1234567890"', vecs["dst_too_long"])   # 10 > 9
        self.assertIn(b"M" * 151, vecs["msg_too_long"])              # 151 > 150

    def test_full_datagram_vector_is_sent_last(self) -> None:
        # On ESP32 the 255-byte datagram wedges the inbound path for good
        # (WiFiUDP::parsePacket() returns 0 while an unread rx_buffer remains),
        # so anything sent after it would be misreported as "never arrived".
        self.assertEqual(ep.rejection_vectors()[-1][0], "full_255_byte_datagram")

    def test_alive_probe_is_rejected_json_that_never_transmits(self) -> None:
        # The liveness probe must be parseable JSON (so it is not confused with
        # a truncation case) yet lack dst, so getExtern() logs it and stops
        # before sendMessage() -- no LoRa frame on the air per probe.
        obj = ep.parse_json(ep.ALIVE_PROBE)
        assert obj is not None
        self.assertNotIn("dst", obj)
        self.assertIn("msg", obj)

    def test_truncated_vector_is_not_parseable(self) -> None:
        vecs = dict(ep.rejection_vectors())
        for name in ("broken_json", "truncated_mid_json"):
            self.assertIsNone(ep.parse_json(vecs[name]), name)


# --------------------------------------------------------- loopback socket


class TestPeerOverLoopback(unittest.TestCase):
    def setUp(self) -> None:
        self.peer = _loopback_peer()
        self.node = FakeNode()

    def tearDown(self) -> None:
        self.peer.stop()
        self.node.close()

    def test_receives_and_parses_with_timestamps(self) -> None:
        t0 = time.monotonic()
        self.node.send(b'{"src_type":"node","type":"pos"}', self.peer)
        got = _wait_count(self.peer, 1)
        self.assertEqual(len(got), 1)
        self.assertEqual(got[0].get("src_type"), "node")
        self.assertGreaterEqual(got[0].t, t0)
        self.assertEqual(got[0].addr[0], "127.0.0.1")

    def test_unparseable_datagram_is_kept_raw(self) -> None:
        # A malformed reply must still be recorded (with obj=None), never
        # dropped: "the node answered garbage" is a finding, not a non-event.
        self.node.send(b"not json at all", self.peer)
        got = _wait_count(self.peer, 1)
        self.assertEqual(len(got), 1)
        self.assertIsNone(got[0].obj)
        self.assertEqual(got[0].text, "not json at all")

    def test_send_dict_is_compact_json_and_bytes_go_verbatim(self) -> None:
        self.peer.send({"type": "msg", "dst": "TEST", "msg": "hi"}, self.node.ip, self.node.port)
        self.assertEqual(self.node.recv(), b'{"type":"msg","dst":"TEST","msg":"hi"}')
        # bytes must not be re-encoded -- the malformed vectors depend on it
        raw = b'{"type":"msg","dst":"TEST","msg":'
        self.peer.send(raw, self.node.ip, self.node.port)
        self.assertEqual(self.node.recv(), raw)

    def test_send_records_length_and_history(self) -> None:
        n = self.peer.send("abc", self.node.ip, self.node.port)
        self.assertEqual(n, 3)
        self.assertEqual(self.node.recv(), b"abc")
        self.assertEqual(len(self.peer.sent), 1)
        self.assertEqual(self.peer.sent[0][2], b"abc")

    def test_full_255_byte_vector_survives_the_wire(self) -> None:
        vec = dict(ep.rejection_vectors())["full_255_byte_datagram"]
        self.assertEqual(self.peer.send(vec, self.node.ip, self.node.port), 255)
        self.assertEqual(self.node.recv(), vec)

    def test_mark_and_since_isolate_one_trigger(self) -> None:
        self.node.send(b'{"type":"pos","src_type":"node"}', self.peer)
        _wait_count(self.peer, 1)
        idx = self.peer.mark()
        self.node.send(b'{"type":"msg","src_type":"lora"}', self.peer)
        _wait_count(self.peer, 2)
        after = self.peer.since(idx)
        self.assertEqual(len(after), 1)
        self.assertEqual(after[0].get("src_type"), "lora")

    def test_wait_for_returns_match_and_times_out(self) -> None:
        self.node.send(b'{"src_type":"lora","type":"msg"}', self.peer)
        hit = self.peer.wait_for(ep.is_out_datagram, timeout=2.0)
        self.assertIsNotNone(hit)
        miss = self.peer.wait_for(lambda d: d.get("type") == "nothing", timeout=0.3)
        self.assertIsNone(miss)

    def test_heartbeats_and_gap_detection_end_to_end(self) -> None:
        for seq in (0, 1, 2, 6):
            self.node.send(b'{"type":"hb","seq":%d,"ms":%d}' % (seq, seq * 500), self.peer)
        _wait_count(self.peer, 4)
        self.assertEqual(self.peer.heartbeats(), [0, 1, 2, 6])
        self.assertEqual(self.peer.heartbeat_gaps(), [(2, 6)])

    def test_heartbeats_ignore_ordinary_traffic(self) -> None:
        self.node.send(b'{"type":"hb","seq":0}', self.peer)
        self.node.send(b'{"src_type":"node","type":"pos"}', self.peer)
        self.node.send(b'{"type":"hb","seq":1}', self.peer)
        _wait_count(self.peer, 3)
        self.assertEqual(self.peer.heartbeats(), [0, 1])
        self.assertEqual(self.peer.heartbeat_gaps(), [])


class TestPeerLifecycle(unittest.TestCase):
    def test_context_manager_binds_and_releases_the_port(self) -> None:
        with ep.ExtUdpPeer(bind_host="127.0.0.1", bind_port=0) as peer:
            port = peer.bind_port
            self.assertGreater(port, 0)
        # after stop() the port is free again: rebinding it must succeed
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            s.bind(("127.0.0.1", port))
        finally:
            s.close()

    def test_send_before_start_raises(self) -> None:
        peer = ep.ExtUdpPeer(bind_host="127.0.0.1", bind_port=0)
        with self.assertRaises(RuntimeError):
            peer.send({"type": "msg"}, "127.0.0.1", 1799)

    def test_stop_is_idempotent(self) -> None:
        peer = _loopback_peer()
        peer.stop()
        peer.stop()


class TestHostIp(unittest.TestCase):
    def test_detect_host_ip_returns_a_dotted_quad(self) -> None:
        ip = ep.detect_host_ip("127.0.0.1")
        self.assertIsNotNone(ip)
        parts = str(ip).split(".")
        self.assertEqual(len(parts), 4)
        self.assertTrue(all(p.isdigit() and 0 <= int(p) <= 255 for p in parts), ip)


if __name__ == "__main__":
    unittest.main()
