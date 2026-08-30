#!/usr/bin/env python3
"""UDP peer for the MeshCom EXTUDP interface (BACKLOG TM-43, UDP-01).

The node's EXTUDP transport is a plain, unauthenticated JSON-over-UDP link on
port 1799 (`EXTERN_PORT`, configuration_global.h:160) in both directions:

    node -> host   {"src_type":"node"|"lora","type":"pos"|"msg"|...}
    host -> node   {"type":"msg","dst":"TEST","msg":"..."}
                   {"type":"tele","temp":23.3,"hum":60,"press":1018.5}

This module is the host end of that link: it binds the port, timestamps every
datagram on the host's monotonic clock, and parses what came back. It is used
by the `extudp` scenario in rak_harness.py and can be driven by hand:

    python3 tools/bench/extudp_peer.py --node-ip 192.168.68.72 --listen 30
    python3 tools/bench/extudp_peer.py --node-ip 192.168.68.72 \
        --send '{"type":"msg","dst":"TEST","msg":"hallo"}' --listen 5
    python3 tools/bench/extudp_peer.py --host-ip        # what to feed --extudpip

With `-D MC_TEST_HOOKS` in the firmware build the node also emits a
sequence-numbered heartbeat every 500 ms (extudp_functions.cpp ~:337). A gap in
that sequence dates a stall to the millisecond and separates "network gone,
loop alive" from "loop task hung" -- see `seq_gaps()`. Stock builds do not
define MC_TEST_HOOKS, so an empty heartbeat list is not a failure.

No third-party dependencies: stdlib + type hints only.
"""

from __future__ import annotations

import argparse
import json
import socket
import subprocess
import sys
import threading
import time
from dataclasses import dataclass, field
from typing import Any, Callable, Dict, List, Optional, Sequence, Tuple, Union

EXTERN_PORT = 1799        # configuration_global.h:160
UDP_TX_BUF_SIZE = 255     # configuration_global.h:162 -- the node's read cap
RECV_BUF = 4096           # host side: read more than the node can ever send

Payload = Union[bytes, str, Dict[str, Any]]


@dataclass
class Datagram:
    """One received datagram with the host's monotonic receive time."""

    t: float
    addr: Tuple[str, int]
    raw: bytes
    obj: Optional[Dict[str, Any]] = None

    @property
    def text(self) -> str:
        return self.raw.decode("utf-8", errors="replace")

    def get(self, key: str, default: Any = None) -> Any:
        return self.obj.get(key, default) if isinstance(self.obj, dict) else default


# --------------------------------------------------------------- parsing


def parse_json(raw: bytes) -> Optional[Dict[str, Any]]:
    """The node's JSON object, or None if the datagram is not a JSON object.

    Anything that is not a top-level object (a bare number, a list, garbage)
    is deliberately None: every documented EXTUDP datagram is an object.
    """
    try:
        obj = json.loads(raw.decode("utf-8", errors="strict"))
    except (UnicodeDecodeError, ValueError):
        return None
    return obj if isinstance(obj, dict) else None


def heartbeat_seq(obj: Optional[Dict[str, Any]]) -> Optional[int]:
    """`seq` of a MC_TEST_HOOKS heartbeat ({"type":"hb","seq":N,"ms":T}), else None."""
    if not isinstance(obj, dict) or obj.get("type") != "hb":
        return None
    seq = obj.get("seq")
    if isinstance(seq, bool) or not isinstance(seq, int):
        return None
    return seq


def seq_gaps(seqs: Sequence[int]) -> List[Tuple[int, int]]:
    """Gaps in a heartbeat sequence as (last_seen, next_seen) pairs.

    Only forward jumps larger than one count. A restart (seq going back to 0)
    is reported as a gap too -- the node rebooting mid-run is exactly the
    failure TM-43 is looking for, and it must not be silently smoothed over.
    """
    gaps: List[Tuple[int, int]] = []
    for prev, cur in zip(seqs, seqs[1:]):
        if cur != prev + 1:
            gaps.append((prev, cur))
    return gaps


def is_out_datagram(dg: Datagram) -> bool:
    """A node -> host datagram from sendExtern() (not a heartbeat, not a notice)."""
    if not isinstance(dg.obj, dict):
        return False
    if dg.obj.get("type") in ("hb", "notice"):
        return False
    return dg.obj.get("src_type") in ("node", "lora")


# ------------------------------------------------------- rejection vectors


def rejection_vectors() -> List[Tuple[str, bytes]]:
    """The six malformed inbound datagrams TM-43 requires the node to survive.

    Each must be logged and rejected by getExtern() without a reset, and the
    console must still echo afterwards. Kept here (not in the harness) so the
    unit tests can pin the payloads without any hardware.

    ORDER MATTERS: `full_255_byte_datagram` comes LAST because on ESP32 it is
    not merely rejected -- it permanently wedges the inbound path (see
    docs/bench-extudp-regression.md: getExternUDP() reads UDP_TX_BUF_SIZE-1 =
    254 of the 255 bytes, and arduino-esp32's WiFiUDP::parsePacket() returns 0
    for good while an unread rx_buffer remains). Any vector after it would be
    reported as "never seen" for a reason that has nothing to do with itself.
    """
    full = b'{"type":"msg","dst":"TEST","msg":"truncated case"}'
    filler = b"F" * (UDP_TX_BUF_SIZE - len(b'{"type":"msg","dst":"TEST","msg":""}'))
    max_dgram = b'{"type":"msg","dst":"TEST","msg":"' + filler + b'"}'
    return [
        ("broken_json", b'{"type":"msg","dst":"TEST","msg":'),
        ("missing_dst", b'{"type":"msg","msg":"kein dst"}'),
        ("missing_msg", b'{"type":"msg","dst":"TEST"}'),
        ("dst_too_long", b'{"type":"msg","dst":"1234567890","msg":"dst 10 chars"}'),
        ("msg_too_long", b'{"type":"msg","dst":"TEST","msg":"' + b"M" * 151 + b'"}'),
        ("truncated_mid_json", full[: len(full) // 2]),
        ("full_255_byte_datagram", max_dgram),
    ]


# A datagram that getExtern() must log and reject without ever putting a frame
# on the air: the cheapest possible "is the inbound path still alive?" probe.
ALIVE_PROBE = b'{"type":"msg","msg":"extudp alive probe"}'


# ------------------------------------------------------------- host address


def detect_host_ip(node_ip: str = "192.168.68.1", iface: str = "en0") -> Optional[str]:
    """The host address the node should send to (`--extudpip <host>`).

    The socket trick first (it names the address of the interface that actually
    routes to the node, which is what the node has to reach), `ipconfig
    getifaddr <iface>` as the macOS fallback.
    """
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            s.connect((node_ip, EXTERN_PORT))   # UDP connect sends nothing
            return str(s.getsockname()[0])
        finally:
            s.close()
    except OSError:
        pass
    try:
        out = subprocess.run(["ipconfig", "getifaddr", iface], capture_output=True,
                             text=True, timeout=5)
        ip = out.stdout.strip()
        return ip or None
    except (OSError, subprocess.SubprocessError):
        return None


# ------------------------------------------------------------------- peer


@dataclass
class ExtUdpPeer:
    """Bound UDP socket plus a receive thread collecting timestamped datagrams."""

    bind_host: str = "0.0.0.0"
    bind_port: int = EXTERN_PORT
    sock: Optional[socket.socket] = None
    received: List[Datagram] = field(default_factory=list)
    sent: List[Tuple[float, Tuple[str, int], bytes]] = field(default_factory=list)
    _thread: Optional[threading.Thread] = None
    _stop: threading.Event = field(default_factory=threading.Event)
    _lock: threading.Lock = field(default_factory=threading.Lock)

    # -- lifecycle -------------------------------------------------------
    def start(self) -> "ExtUdpPeer":
        if self.sock is None:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            s.bind((self.bind_host, self.bind_port))
            self.sock = s
        self.sock.settimeout(0.2)
        self.bind_port = self.sock.getsockname()[1]
        self._stop.clear()
        self._thread = threading.Thread(target=self._rx_loop, name="extudp-rx", daemon=True)
        self._thread.start()
        return self

    def stop(self) -> None:
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=2.0)
            self._thread = None
        if self.sock is not None:
            try:
                self.sock.close()
            finally:
                self.sock = None

    def __enter__(self) -> "ExtUdpPeer":
        return self.start()

    def __exit__(self, *exc: Any) -> None:
        self.stop()

    def _rx_loop(self) -> None:
        while not self._stop.is_set():
            try:
                raw, addr = self.sock.recvfrom(RECV_BUF)   # type: ignore[union-attr]
            except (socket.timeout, TimeoutError):
                continue
            except OSError:
                return
            dg = Datagram(t=time.monotonic(), addr=addr, raw=raw, obj=parse_json(raw))
            with self._lock:
                self.received.append(dg)

    # -- send ------------------------------------------------------------
    def send(self, payload: Payload, node_ip: str, port: int = EXTERN_PORT) -> int:
        """Send one datagram to the node. dict -> JSON; str -> utf-8; bytes verbatim.

        bytes stay untouched on purpose: the malformed vectors (truncated JSON,
        a full 255-byte datagram) must go out exactly as constructed.
        """
        if isinstance(payload, dict):
            data = json.dumps(payload, separators=(",", ":")).encode("utf-8")
        elif isinstance(payload, str):
            data = payload.encode("utf-8")
        else:
            data = bytes(payload)
        if self.sock is None:
            raise RuntimeError("peer not started")
        self.sock.sendto(data, (node_ip, port))
        self.sent.append((time.monotonic(), (node_ip, port), data))
        return len(data)

    # -- read back -------------------------------------------------------
    def mark(self) -> int:
        """Index into `received` -- take one before a trigger, read `since=` after."""
        with self._lock:
            return len(self.received)

    def since(self, idx: int = 0) -> List[Datagram]:
        with self._lock:
            return list(self.received[idx:])

    def wait_for(self, predicate: Callable[[Datagram], bool], timeout: float,
                 since: int = 0) -> Optional[Datagram]:
        end = time.monotonic() + timeout
        while True:
            for dg in self.since(since):
                if predicate(dg):
                    return dg
            if time.monotonic() >= end:
                return None
            time.sleep(0.05)

    def heartbeats(self, since: int = 0) -> List[int]:
        seqs = [heartbeat_seq(dg.obj) for dg in self.since(since)]
        return [s for s in seqs if s is not None]

    def heartbeat_gaps(self, since: int = 0) -> List[Tuple[int, int]]:
        return seq_gaps(self.heartbeats(since))


# -------------------------------------------------------------------- CLI


def main(argv: Optional[Sequence[str]] = None) -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--bind", default="0.0.0.0")
    p.add_argument("--port", type=int, default=EXTERN_PORT)
    p.add_argument("--node-ip", default="", help="node address to send to")
    p.add_argument("--send", default="", help="one datagram to send (raw string, JSON or not)")
    p.add_argument("--reject", action="store_true",
                   help="send all seven TM-43 rejection vectors, one per second")
    p.add_argument("--listen", type=float, default=10.0, help="seconds to listen (default 10)")
    p.add_argument("--host-ip", action="store_true",
                   help="print the host address to feed --extudpip and exit")
    args = p.parse_args(argv)

    if args.host_ip:
        ip = detect_host_ip(args.node_ip or "192.168.68.1")
        print(ip or "unknown")
        return 0 if ip else 1

    with ExtUdpPeer(bind_host=args.bind, bind_port=args.port) as peer:
        print(f"listening on {args.bind}:{peer.bind_port}", file=sys.stderr)
        if args.send:
            if not args.node_ip:
                print("--send needs --node-ip", file=sys.stderr)
                return 2
            n = peer.send(args.send, args.node_ip, args.port)
            print(f"sent {n} B to {args.node_ip}:{args.port}", file=sys.stderr)
        if args.reject:
            if not args.node_ip:
                print("--reject needs --node-ip", file=sys.stderr)
                return 2
            for name, vec in rejection_vectors():
                peer.send(vec, args.node_ip, args.port)
                print(f"sent vector {name} ({len(vec)} B)", file=sys.stderr)
                time.sleep(1.0)
        t0 = time.monotonic()
        seen = 0
        while time.monotonic() - t0 < args.listen:
            for dg in peer.since(seen):
                print(f"{dg.t - t0:8.3f} {dg.addr[0]:>15s} {len(dg.raw):4d} B  {dg.text[:160]}")
            seen = peer.mark()
            time.sleep(0.1)
        hb = peer.heartbeats()
        if hb:
            print(f"heartbeats: {len(hb)} seq {hb[0]}..{hb[-1]} gaps={seq_gaps(hb)}",
                  file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
