"""extudp_sniff.py parser: a hand-built Ethernet/IPv4/UDP frame round-trips,
everything that is not a first-fragment IPv4 UDP datagram is None."""
from __future__ import annotations

import json
import socket
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import extudp_sniff as sn  # noqa: E402


def _frame(payload: bytes, *, src="192.168.68.63", dst="192.168.68.74", sport=1799, dport=1799,
           eth_type=0x0800, proto=17, frag=0, ihl=5) -> bytes:
    eth = b"\x00" * 12 + struct.pack("!H", eth_type)
    udp_len = 8 + len(payload)
    udp = struct.pack("!HHHH", sport, dport, udp_len, 0) + payload
    total = ihl * 4 + udp_len
    ip = struct.pack("!BBHHHBBH4s4s", (4 << 4) | ihl, 0, total, 0x1234, frag, 64, proto, 0,
                     socket.inet_aton(src), socket.inet_aton(dst))
    ip += b"\x00" * (ihl * 4 - 20)
    return eth + ip + udp


def test_parse_ipv4_udp_round_trips_addresses_ports_and_payload() -> None:
    text = '{"src_type":"lora","type":"pos","src":"DK5EN-90"}'.encode()
    fr = sn.parse_frame(_frame(text))
    assert fr == sn.UdpFrame("192.168.68.63", 1799, "192.168.68.74", 1799, text)


def test_parse_handles_ip_options_and_trailing_ethernet_padding() -> None:
    text = b"x" * 20
    frame = _frame(text, ihl=6) + b"\x00" * 7      # options + padding past total_len
    fr = sn.parse_frame(frame)
    assert fr is not None and fr.payload == text


def test_parse_rejects_non_ip_non_udp_fragments_and_short_frames() -> None:
    assert sn.parse_frame(_frame(b"abc", eth_type=0x0806)) is None       # ARP
    assert sn.parse_frame(_frame(b"abc", eth_type=0x86DD)) is None       # IPv6
    assert sn.parse_frame(_frame(b"abc", proto=6)) is None               # TCP
    assert sn.parse_frame(_frame(b"abc", frag=0x0010)) is None           # 2nd fragment
    assert sn.parse_frame(_frame(b"abc", frag=0x2000)) is not None       # MF set, offset 0
    assert sn.parse_frame(b"\x00" * 30) is None


def test_record_is_one_json_line_with_wall_clock_and_text() -> None:
    fr = sn.UdpFrame("10.0.0.2", 1799, "10.0.0.1", 1799, "grüß".encode())
    line = sn.record(fr, now=1_800_000_000.25)
    d = json.loads(line)
    assert d["src"] == "10.0.0.2:1799" and d["dst"] == "10.0.0.1:1799"
    assert d["len"] == 6 and d["text"] == "grüß"
    assert d["t"].endswith(".250") and "T" in d["t"] and "\n" not in line
