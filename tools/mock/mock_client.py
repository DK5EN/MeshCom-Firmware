#!/usr/bin/env python3
"""Minimal softnode-style client -- imitates the node -> server half of the
MeshCom server UDP protocol (docs/architecture/11-wire-format.md §2) for
tests and manual protocol validation.

Sends a valid KEEP heartbeat, receives the server's BEAT, can send a
DATA-wrapped LoRa frame (frame bytes supplied as hex), and prints anything
the server sends back (BEAT/GATE/CONF) as hex plus a short parsed summary.

Byte layouts cross-checked against
``/Users/martinwerner/WebDev/mc-chat/meshcom_mock/protocol.py`` and
``node.py`` (read-only reference, not imported).

Stdlib only, Python 3.
"""

from __future__ import annotations

import argparse
import socket

DEFAULT_VERSION = "4.35"
DEFAULT_SUB = "p"
DEFAULT_CALLSIGN = "MOCK-01"
DATA_HEADER_LEN = 36


# ---------------------------------------------------------------------------
# Datagram builders (node -> server)
# ---------------------------------------------------------------------------


def build_keep(
    gateway_id: int,
    callsign: str,
    version: str = DEFAULT_VERSION,
    sub: str = DEFAULT_SUB,
    groups: list[str] | None = None,
) -> bytes:
    """Build a KEEP heartbeat, byte-identical to sendKEEP()
    (src/udp_functions.cpp:1113):

    "KEEP" + %08X gw_id + %-9.9s callsign + %-4.4s version + %-1.1s sub
           + <grc_ids ";"-joined> + 0x00
    """
    group_str = "".join(f"{g};" for g in (groups or []))
    body = (
        f"KEEP{gateway_id & 0xFFFFFFFF:08X}"
        f"{callsign:<9.9}"
        f"{version:<4.4}"
        f"{sub:<1.1}"
        f"{group_str}"
    )
    return body.encode("ascii") + b"\x00"


def build_data(
    gateway_id: int,
    callsign: str,
    frame: bytes,
    *,
    version: str = DEFAULT_VERSION,
    sub: str = DEFAULT_SUB,
    rssi: int = 0,
    snr: int = 0,
    modulation: str = "03",
) -> bytes:
    """Build a DATA envelope, byte-identical to addNodeData()
    (src/udp_functions.cpp:1154): a fixed 36-byte ASCII header followed by
    the raw LoRa frame.
    """
    mod_str = modulation[:2].ljust(2)
    header = (
        f"DATA{gateway_id & 0xFFFFFFFF:08X}"
        f"{callsign:<9.9}"
        f"{version:<4.4}"
        f"{sub:<1.1}"
        f"{rssi:4d}"
        f"{snr:4d}"
        f"{mod_str}"
    )
    header_b = header.encode("ascii")
    if len(header_b) != DATA_HEADER_LEN:
        raise ValueError(
            f"DATA header must be {DATA_HEADER_LEN} bytes, built {len(header_b)}: {header_b!r}"
        )
    return header_b + frame


# ---------------------------------------------------------------------------
# Received-datagram summary (server -> node)
# ---------------------------------------------------------------------------


def summarize(data: bytes) -> str:
    """Human-readable one-line summary of a received BEAT/GATE/CONF datagram."""
    if data.startswith(b"BEAT"):
        if len(data) >= 6 and data[4] == 0x00:
            call_len = data[5]
            callsign = data[6 : 6 + call_len].decode("ascii", errors="replace")
            rest = data[6 + call_len :]
            status = ""
            if len(rest) >= 2 and rest[0] == 0x01:
                slen = rest[1]
                status = rest[2 : 2 + slen].decode("ascii", errors="replace")
            summary = f"BEAT callsign={callsign!r}"
            if status:
                summary += f" status={status!r}"
            return summary
        return f"BEAT ({len(data)}B, no structured TLV)"
    if data.startswith(b"GATE"):
        frame = data[4:]
        return f"GATE frame_len={len(frame)}"
    if data.startswith(b"CONF"):
        return f"CONF raw_len={len(data) - 4}"
    return f"UNKNOWN indicator={data[:4]!r} len={len(data)}"


# ---------------------------------------------------------------------------
# Client
# ---------------------------------------------------------------------------


class MockNode:
    """Sends KEEP/DATA to a MeshCom (mock) server and reports whatever it
    answers with."""

    def __init__(
        self,
        server_host: str,
        server_port: int,
        *,
        gateway_id: int,
        callsign: str = DEFAULT_CALLSIGN,
        version: str = DEFAULT_VERSION,
        sub: str = DEFAULT_SUB,
        timeout: float = 2.0,
    ) -> None:
        self.server_addr = (server_host, server_port)
        self.gateway_id = gateway_id
        self.callsign = callsign
        self.version = version
        self.sub = sub

        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("0.0.0.0", 0))
        self.sock.settimeout(timeout)

    def close(self) -> None:
        self.sock.close()

    def send_keep(self, groups: list[str] | None = None) -> bytes:
        datagram = build_keep(self.gateway_id, self.callsign, self.version, self.sub, groups)
        self.sock.sendto(datagram, self.server_addr)
        return datagram

    def send_data(self, frame: bytes, *, rssi: int = 0, snr: int = 0) -> bytes:
        datagram = build_data(
            self.gateway_id,
            self.callsign,
            frame,
            version=self.version,
            sub=self.sub,
            rssi=rssi,
            snr=snr,
        )
        self.sock.sendto(datagram, self.server_addr)
        return datagram

    def recv(self, bufsize: int = 4096) -> bytes:
        data, _addr = self.sock.recvfrom(bufsize)
        return data


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Minimal MeshCom node stand-in for manual protocol testing"
    )
    parser.add_argument("--server-host", default="127.0.0.1")
    parser.add_argument("--server-port", type=int, default=1990)
    parser.add_argument(
        "--gateway-id", default="0x48A4690D", help="hex gateway id, e.g. 0x48A4690D"
    )
    parser.add_argument("--callsign", default=DEFAULT_CALLSIGN)
    parser.add_argument(
        "--frame-hex", default=None, help="raw LoRa frame as hex to send via DATA"
    )
    parser.add_argument("--timeout", type=float, default=2.0)
    args = parser.parse_args(argv)

    gateway_id = int(args.gateway_id, 16)
    node = MockNode(
        args.server_host,
        args.server_port,
        gateway_id=gateway_id,
        callsign=args.callsign,
        timeout=args.timeout,
    )
    try:
        node.send_keep()
        print(f"-> KEEP sent (gw={gateway_id:08X} call={args.callsign})")
        try:
            beat = node.recv()
            print(f"<- {summarize(beat)}  ({beat.hex()})")
        except socket.timeout:
            print("<- no BEAT received (timeout)")

        if args.frame_hex:
            frame = bytes.fromhex(args.frame_hex)
            node.send_data(frame)
            print(f"-> DATA sent (frame_len={len(frame)})")

        while True:
            try:
                data = node.recv()
            except socket.timeout:
                break
            print(f"<- {summarize(data)}  ({data.hex()})")
    finally:
        node.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
