#!/usr/bin/env python3
"""Passive Extern-UDP logger: sniff port 1799 next to the live consumer.

DK5EN-98 feeds its Extern-UDP stream (JSON over UDP 1799, extudp_peer.py has
the protocol) to the production host `mcapp`, whose own process holds the
socket. A soak must not redirect that feed and cannot bind the port a second
time, so this tool reads the frames off the wire instead: an AF_PACKET raw
socket (Linux, needs root), IPv4 + UDP parsed by hand, every datagram to or
from the port appended to a JSON-lines file as it arrives (one `write` +
`flush` per datagram, so a killed run keeps everything up to the kill).

    sudo python3 extudp_sniff.py --hours 24 --out ~/extudp/dk5en-98.jsonl
    sudo python3 extudp_sniff.py --iface eth0 --port 1799 --hours 0.01

Record line: {"t": "<ISO wall clock>", "src": "ip:port", "dst": "ip:port",
"len": N, "text": "<payload, utf-8 with replacement>"}. The payload is not
parsed here; the evaluation (RX-01, docs/BACKLOG.md) joins `t` with the node's
2323 console log, which the rpizero records in parallel.

Stdlib only; the parser is pure and tested (test_extudp_sniff.py), the socket
part runs only on Linux.
"""
from __future__ import annotations

import argparse
import json
import socket
import struct
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

EXTERN_PORT = 1799
ETH_P_IP = 0x0800
ETH_HDR = 14
IP_PROTO_UDP = 17


@dataclass(frozen=True)
class UdpFrame:
    src_ip: str
    src_port: int
    dst_ip: str
    dst_port: int
    payload: bytes


def parse_frame(frame: bytes) -> Optional[UdpFrame]:
    """Ethernet II -> IPv4 -> UDP; None for anything else (ARP, IPv6, TCP,
    fragments after the first, truncated frames)."""
    if len(frame) < ETH_HDR + 20 + 8:
        return None
    eth_type = struct.unpack("!H", frame[12:14])[0]
    if eth_type != ETH_P_IP:
        return None
    ip = frame[ETH_HDR:]
    ver_ihl = ip[0]
    if ver_ihl >> 4 != 4:
        return None
    ihl = (ver_ihl & 0x0F) * 4
    if ihl < 20 or len(ip) < ihl + 8:
        return None
    total_len = struct.unpack("!H", ip[2:4])[0]
    flags_frag = struct.unpack("!H", ip[6:8])[0]
    if flags_frag & 0x1FFF:          # fragment offset != 0: not the first fragment
        return None
    if ip[9] != IP_PROTO_UDP:
        return None
    src_ip = socket.inet_ntoa(ip[12:16])
    dst_ip = socket.inet_ntoa(ip[16:20])
    udp = ip[ihl:total_len] if total_len >= ihl + 8 else ip[ihl:]
    src_port, dst_port, udp_len = struct.unpack("!HHH", udp[0:6])
    payload = udp[8:udp_len] if 8 <= udp_len <= len(udp) else udp[8:]
    return UdpFrame(src_ip, src_port, dst_ip, dst_port, bytes(payload))


def record(frame: UdpFrame, now: Optional[float] = None) -> str:
    """One JSON line for a frame; `now` is a wall-clock epoch (test hook)."""
    t = time.strftime("%Y-%m-%dT%H:%M:%S", time.localtime(now)) + ("%.3f" % ((now or time.time()) % 1))[1:]
    return json.dumps({
        "t": t,
        "src": f"{frame.src_ip}:{frame.src_port}",
        "dst": f"{frame.dst_ip}:{frame.dst_port}",
        "len": len(frame.payload),
        "text": frame.payload.decode("utf-8", errors="replace"),
    }, ensure_ascii=False)


def sniff(iface: Optional[str], port: int, hours: float, out: Path) -> int:
    """Linux only: AF_PACKET needs CAP_NET_RAW (run under sudo)."""
    sock = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(ETH_P_IP))  # type: ignore[attr-defined]
    if iface:
        sock.bind((iface, 0))
    sock.settimeout(1.0)
    out.parent.mkdir(parents=True, exist_ok=True)
    deadline = time.monotonic() + hours * 3600.0
    n = 0
    with open(out, "a", encoding="utf-8") as f:
        print(f"sniffing udp port {port} on {iface or 'all interfaces'} for {hours} h -> {out}",
              file=sys.stderr, flush=True)
        while time.monotonic() < deadline:
            try:
                raw = sock.recv(65535)
            except socket.timeout:
                continue
            fr = parse_frame(raw)
            if fr is None or (fr.src_port != port and fr.dst_port != port):
                continue
            f.write(record(fr, time.time()) + "\n")
            f.flush()
            n += 1
    print(f"done: {n} datagram(s) -> {out}", file=sys.stderr)
    return 0


def main(argv: Optional[list[str]] = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--iface", default=None, help="interface to bind (default: all)")
    ap.add_argument("--port", type=int, default=EXTERN_PORT)
    ap.add_argument("--hours", type=float, default=24.0)
    ap.add_argument("--out", type=Path, required=True, help="JSON-lines file, appended")
    a = ap.parse_args(argv)
    if not hasattr(socket, "AF_PACKET"):
        print("extudp_sniff: AF_PACKET raw sockets exist on Linux only", file=sys.stderr)
        return 2
    return sniff(a.iface, a.port, a.hours, a.out)


if __name__ == "__main__":
    raise SystemExit(main())
