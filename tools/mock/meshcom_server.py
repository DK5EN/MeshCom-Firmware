#!/usr/bin/env python3
"""Mock MeshCom server -- test double for the node<->server UDP protocol
(port 1990, ``UDP_PORT`` in ``src/configuration_global.h``).

Implements the server side of ``docs/architecture/11-wire-format.md`` §2:

* node -> server: ``KEEP`` (heartbeat), ``DATA`` (LoRa frame envelope)
* server -> node: ``BEAT`` (heartbeat ack), ``GATE`` (frame to transmit),
  ``CONF`` (config TLV, built for tests but not spontaneously emitted here)

Every ``KEEP`` is answered with a ``BEAT`` immediately -- real nodes give up
and reconnect after ``MAX_HB_RX_TIME`` = 65 s of silence
(``src/configuration_global.h:159``), so a mock that stalls a heartbeat
response breaks any client under test.

Stdlib only, Python 3. See ``tools/mock/README.md`` for usage and the list of
places this mock intentionally follows the doc's plain-English wire format
description rather than a firmware quirk (see ``_has_excess_zero_run``
below).
"""

from __future__ import annotations

import argparse
import logging
import re
import socket
import struct
import threading
import time
from dataclasses import dataclass, field

# ---------------------------------------------------------------------------
# Protocol constants (docs/architecture/11-wire-format.md §2)
# ---------------------------------------------------------------------------

UDP_MSG_INDICATOR_LEN = 4
DATA_HEADER_LEN = 36  # doc 11 §2.1: 4+8+9+4+1+4+4+2 = 36
MAX_ZEROS = 6  # src/configuration_global.h:156
DEFAULT_PORT = 1990  # src/configuration_global.h:86 UDP_PORT
DEFAULT_SERVER_CALLSIGN = "MOCK-SRV"
REGISTRY_TTL = 120.0  # seconds; brief's expiry window for stale clients

_KEEP_PREFIX = b"KEEP"
_DATA_PREFIX = b"DATA"
_BEAT_PREFIX = b"BEAT"
_GATE_PREFIX = b"GATE"
_CONF_PREFIX = b"CONF"

_HEX8_RE = re.compile(rb"^[0-9A-Fa-f]{8}$")

# KEEP layout: "KEEP" + %08X gw_id(8) + %-9.9s callsign(9) + %-4.4s version(4)
#            + %-1.1s sub(1) + <grc_ids> + 0x00   (sendKEEP(), udp_functions.cpp:1113)
_KEEP_FIXED_LEN = 4 + 8 + 9 + 4 + 1  # = 26, before the variable group-id tail

logger = logging.getLogger("meshcom_mock.server")


# ---------------------------------------------------------------------------
# Errors and small value types
# ---------------------------------------------------------------------------


class KeepParseError(ValueError):
    """Raised when a KEEP datagram fails validation."""


class DataHeaderError(ValueError):
    """Raised when a DATA header fails validation."""


@dataclass
class ClientInfo:
    """One registered gateway node, as learned from its KEEP datagrams."""

    addr: tuple[str, int]
    gateway_id: str
    callsign: str
    version: str
    sub: str
    groups: list[str]
    last_seen: float = field(default_factory=time.time)


@dataclass
class DataHeader:
    """Parsed fields of a validated 36-byte DATA header."""

    gateway_id: str
    callsign: str
    version: str
    sub: str
    rssi: int
    snr: int
    modulation: str


# ---------------------------------------------------------------------------
# Corrupt-datagram filter
# ---------------------------------------------------------------------------


def _has_excess_zero_run(data: bytes) -> bool:
    """True if ``data`` contains a run of more than MAX_ZEROS (6) consecutive
    0x00 bytes -- doc 11 §2.2's stated ingress filter: "Datagrams with more
    than MAX_ZEROS = 6 consecutive zero bytes are discarded as corrupt."

    DOC-11 DISCREPANCY (reported, not silently fixed -- the doc is the
    contract this mock implements): the actual firmware check
    (``src/udp_functions.cpp:122-136``, cloned at
    ``src/nrf52/nrf_eth.cpp:210-246``) is narrower than this prose. It walks
    the buffer in non-overlapping 2-byte steps, resets a counter to 0 on any
    step that is not an all-zero pair, and only compares the counter's
    *final* value (i.e. the length of the trailing run of zero-byte pairs,
    pair-aligned to an even offset) against MAX_ZEROS -- not the longest run
    anywhere in the datagram. A 7+ zero-byte run in the *middle* of an
    otherwise well-formed firmware datagram, followed by non-zero bytes,
    would NOT be rejected by the real firmware, only by this literal reading
    of the doc. See README.md.
    """
    run = 0
    for b in data:
        if b == 0x00:
            run += 1
            if run > MAX_ZEROS:
                return True
        else:
            run = 0
    return False


# ---------------------------------------------------------------------------
# KEEP / DATA parsing (node -> server)
# ---------------------------------------------------------------------------


def parse_keep(payload: bytes) -> dict:
    """Parse a KEEP heartbeat. Raises KeepParseError on malformed input.

    Layout (doc 11 §2.1): "KEEP" + %08X gw_id(8) + %-9.9s callsign(9)
    + %-4.4s version(4) + %-1.1s sub(1) + grc_ids(";"-joined) + 0x00.
    """
    if len(payload) < _KEEP_FIXED_LEN or not payload.startswith(_KEEP_PREFIX):
        raise KeepParseError(f"KEEP too short or bad prefix ({len(payload)}B)")

    gw_hex = payload[4:12]
    if not _HEX8_RE.match(gw_hex):
        raise KeepParseError(f"KEEP gateway_id not 8 hex chars: {gw_hex!r}")

    gateway_id = gw_hex.decode("ascii").upper()
    callsign = payload[12:21].decode("ascii", errors="replace").rstrip(" ")
    version = payload[21:25].decode("ascii", errors="replace").rstrip(" ")
    sub = payload[25:26].decode("ascii", errors="replace")

    rest = payload[26:]
    nul = rest.find(b"\x00")
    if nul != -1:
        rest = rest[:nul]
    group_str = rest.decode("ascii", errors="replace")
    groups = [g for g in group_str.split(";") if g]

    return {
        "gateway_id": gateway_id,
        "callsign": callsign,
        "version": version,
        "sub": sub,
        "groups": groups,
    }


def parse_data_header(payload: bytes) -> DataHeader:
    """Validate and parse the fixed 36-byte DATA header field-by-field.

    Layout (doc 11 §2.1): "DATA" + %08X gw_id(8) + %-9.9s callsign(9)
    + %-4.4s version(4) + %-1.1s sub(1) + %4i rssi(4) + %4i snr(4)
    + 2 ASCII modulation digits(2) = 36 bytes, followed by the raw LoRa frame.
    """
    if len(payload) < DATA_HEADER_LEN:
        raise DataHeaderError(
            f"DATA header too short: {len(payload)} < {DATA_HEADER_LEN}"
        )
    if not payload.startswith(_DATA_PREFIX):
        raise DataHeaderError("DATA bad prefix")

    header = payload[:DATA_HEADER_LEN]

    gw_hex = header[4:12]
    if not _HEX8_RE.match(gw_hex):
        raise DataHeaderError(f"DATA gateway_id not 8 hex chars: {gw_hex!r}")
    gateway_id = gw_hex.decode("ascii").upper()

    callsign = header[12:21].decode("ascii", errors="replace").rstrip(" ")
    version = header[21:25].decode("ascii", errors="replace").rstrip(" ")
    sub = header[25:26].decode("ascii", errors="replace")

    rssi_raw = header[26:30]
    snr_raw = header[30:34]
    mod_raw = header[34:36]

    try:
        rssi = int(rssi_raw.decode("ascii"))
        snr = int(snr_raw.decode("ascii"))
    except (ValueError, UnicodeDecodeError) as exc:
        raise DataHeaderError(
            f"DATA rssi/snr not a 4-char integer: {rssi_raw!r}/{snr_raw!r}"
        ) from exc

    if not mod_raw.isdigit():
        raise DataHeaderError(f"DATA modulation not 2 ASCII digits: {mod_raw!r}")
    modulation = mod_raw.decode("ascii")

    return DataHeader(gateway_id, callsign, version, sub, rssi, snr, modulation)


# ---------------------------------------------------------------------------
# BEAT / GATE / CONF builders (server -> node)
# ---------------------------------------------------------------------------


def build_beat_datagram(callsign: str, status: str | None = None) -> bytes:
    """Build a byte-exact BEAT datagram (doc 11 §2.2):

    "BEAT" + 0x00 + <call_len 1B> + <callsign> [+ 0x01 + <status_len 1B> + <status>]
    """
    call_b = callsign.encode("ascii")
    if len(call_b) > 255:
        raise ValueError("callsign too long for a 1-byte length prefix")

    out = bytearray(_BEAT_PREFIX)
    out += b"\x00"
    out.append(len(call_b))
    out += call_b

    if status is not None:
        status_b = status.encode("ascii")
        if len(status_b) > 255:
            raise ValueError("status too long for a 1-byte length prefix")
        out += b"\x01"
        out.append(len(status_b))
        out += status_b

    return bytes(out)


def build_gate_datagram(frame: bytes) -> bytes:
    """Build a byte-exact GATE datagram: "GATE" + raw LoRa frame."""
    return _GATE_PREFIX + frame


def build_conf_datagram(
    callsign: str, shortname: str, lat: int, lon: int, alt: int
) -> bytes:
    """Build a byte-exact CONF datagram (doc 11 §2.2 / nrf_eth.cpp:497-587):

    0x00 <len> <callsign>   0x01 <len> <shortname>
    0x02 <int32 LE lat>     0x03 <int32 LE lon>     0x04 <int32 LE alt>
    """
    call_b = callsign.encode("ascii")
    short_b = shortname.encode("ascii")
    if len(call_b) > 255 or len(short_b) > 255:
        raise ValueError("callsign/shortname too long for a 1-byte length prefix")

    out = bytearray(_CONF_PREFIX)
    out += b"\x00"
    out.append(len(call_b))
    out += call_b
    out += b"\x01"
    out.append(len(short_b))
    out += short_b
    out += b"\x02"
    out += struct.pack("<i", lat)
    out += b"\x03"
    out += struct.pack("<i", lon)
    out += b"\x04"
    out += struct.pack("<i", alt)
    return bytes(out)


# ---------------------------------------------------------------------------
# Server
# ---------------------------------------------------------------------------


class MockMeshComServer:
    """A minimal, in-process-testable stand-in for the MeshCom server."""

    def __init__(
        self,
        host: str = "0.0.0.0",
        port: int = DEFAULT_PORT,
        *,
        callsign: str = DEFAULT_SERVER_CALLSIGN,
        beat_status: str | None = None,
        verbose: bool = False,
        registry_ttl: float = REGISTRY_TTL,
    ) -> None:
        self.host = host
        self.callsign = callsign
        self.beat_status = beat_status
        self.verbose = verbose
        self.registry_ttl = registry_ttl

        self.clients: dict[tuple[str, int], ClientInfo] = {}
        self._lock = threading.Lock()

        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind((host, port))
        self.sock.settimeout(0.1)
        self.port = self.sock.getsockname()[1]

        self._stop = threading.Event()
        self._thread: threading.Thread | None = None

        logger.setLevel(logging.DEBUG if verbose else logging.INFO)

    # -- lifecycle ----------------------------------------------------

    def start(self) -> None:
        """Run the receive loop on a background thread (for tests)."""
        self._thread = threading.Thread(
            target=self.serve_forever, daemon=True, name="mock-meshcom-server"
        )
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=2.0)
        self.sock.close()

    def serve_forever(self) -> None:
        """Block, handling datagrams until stop() is called (for CLI use)."""
        while not self._stop.is_set():
            try:
                data, addr = self.sock.recvfrom(4096)
            except socket.timeout:
                continue
            except OSError:
                break
            self._handle_datagram(data, addr)

    # -- dispatch -------------------------------------------------------

    def _handle_datagram(self, data: bytes, addr: tuple[str, int]) -> None:
        self._expire_clients()

        if _has_excess_zero_run(data):
            logger.info(
                "event=drop reason=max_zeros addr=%s:%s len=%d",
                addr[0], addr[1], len(data),
            )
            return

        if len(data) < UDP_MSG_INDICATOR_LEN:
            logger.info(
                "event=drop reason=too_short addr=%s:%s len=%d",
                addr[0], addr[1], len(data),
            )
            return

        indicator = data[:4]
        if indicator == _KEEP_PREFIX:
            self._handle_keep(data, addr)
        elif indicator == _DATA_PREFIX:
            self._handle_data(data, addr)
        else:
            logger.info(
                "event=drop reason=unknown_indicator indicator=%r addr=%s:%s",
                indicator, addr[0], addr[1],
            )

    def _expire_clients(self) -> None:
        now = time.time()
        with self._lock:
            stale = [
                a
                for a, c in self.clients.items()
                if now - c.last_seen > self.registry_ttl
            ]
            for a in stale:
                logger.info(
                    "event=expire addr=%s:%s callsign=%s",
                    a[0], a[1], self.clients[a].callsign,
                )
                del self.clients[a]

    def _handle_keep(self, data: bytes, addr: tuple[str, int]) -> None:
        try:
            fields = parse_keep(data)
        except KeepParseError as exc:
            logger.info(
                "event=drop reason=bad_keep addr=%s:%s error=%s",
                addr[0], addr[1], exc,
            )
            return

        with self._lock:
            self.clients[addr] = ClientInfo(addr=addr, **fields)

        logger.info(
            "event=keep addr=%s:%s gw=%s callsign=%s ver=%s%s groups=%s",
            addr[0], addr[1], fields["gateway_id"], fields["callsign"],
            fields["version"], fields["sub"], fields["groups"],
        )
        self._send_beat(addr)

    def _send_beat(self, addr: tuple[str, int]) -> None:
        datagram = build_beat_datagram(self.callsign, self.beat_status)
        self.sock.sendto(datagram, addr)
        logger.debug("event=beat addr=%s:%s bytes=%d", addr[0], addr[1], len(datagram))

    def _handle_data(self, data: bytes, addr: tuple[str, int]) -> None:
        try:
            header = parse_data_header(data)
        except DataHeaderError as exc:
            logger.info(
                "event=drop reason=bad_data_header addr=%s:%s error=%s",
                addr[0], addr[1], exc,
            )
            return

        frame = data[DATA_HEADER_LEN:]
        logger.info(
            "event=data addr=%s:%s gw=%s callsign=%s rssi=%d snr=%d mod=%s frame_len=%d",
            addr[0], addr[1], header.gateway_id, header.callsign,
            header.rssi, header.snr, header.modulation, len(frame),
        )

        with self._lock:
            targets = [a for a in self.clients if a != addr]
        for target in targets:
            self.send_gate(frame, target)

    # -- test-driving helpers (importable by tests / manual scripts) --

    def send_gate(self, frame_bytes: bytes, to_addr: tuple[str, int]) -> bytes:
        """Build and send a byte-exact GATE datagram; returns the bytes sent."""
        datagram = build_gate_datagram(frame_bytes)
        self.sock.sendto(datagram, to_addr)
        logger.debug(
            "event=gate addr=%s:%s bytes=%d", to_addr[0], to_addr[1], len(datagram)
        )
        return datagram

    def send_conf(
        self,
        addr: tuple[str, int],
        callsign: str,
        shortname: str,
        lat: int,
        lon: int,
        alt: int,
    ) -> bytes:
        """Build and send a byte-exact CONF datagram; returns the bytes sent."""
        datagram = build_conf_datagram(callsign, shortname, lat, lon, alt)
        self.sock.sendto(datagram, addr)
        logger.debug(
            "event=conf addr=%s:%s bytes=%d", addr[0], addr[1], len(datagram)
        )
        return datagram


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Mock MeshCom server (node<->server UDP protocol, port 1990)"
    )
    parser.add_argument("--host", default="0.0.0.0", help="bind address (default: 0.0.0.0)")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help="UDP port (default: 1990)")
    parser.add_argument("--callsign", default=DEFAULT_SERVER_CALLSIGN, help="server-side callsign sent in BEAT")
    parser.add_argument("--beat-status", default=None, help="optional status string added to the BEAT TLV")
    parser.add_argument("--registry-ttl", type=float, default=REGISTRY_TTL, help="seconds before an idle client is expired")
    parser.add_argument("--verbose", action="store_true", help="debug logging")
    args = parser.parse_args(argv)

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(name)s %(message)s",
    )

    server = MockMeshComServer(
        args.host,
        args.port,
        callsign=args.callsign,
        beat_status=args.beat_status,
        verbose=args.verbose,
        registry_ttl=args.registry_ttl,
    )
    logger.info(
        "event=listen host=%s port=%d callsign=%s", args.host, server.port, args.callsign
    )
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.sock.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
