#!/usr/bin/env python3
"""Refuse bench tests on a node that has no valid identity.

Operator rules (2026-09-26, after two bench nodes ran a whole campaign as
XX0XXX without WiFi):

- no test on a node whose callsign is XX0XXX or empty -- such a node never
  transmits, so every TX-side result from it is void;
- the callsign must be the one registered for that node in fleet.json, so no
  two nodes share an SSID;
- bench TX power at most ``max_txpower_dbm`` (2 dBm);
- an ESP32 node needs a WiFi IP, a set clock and the web server -- without
  WiFi there is no NTP and no web server, and the web and clock paths go
  untested.

Library use (harnesses call this before the first test step)::

    from identity_guard import require
    require(info_text, "t-beam-92")          # raises IdentityError

CLI::

    identity_guard.py --node t-beam-92 --host 192.168.68.71   # 2323 net console
    identity_guard.py --node rak-90 --port /dev/cu.usbmodem1101
    identity_guard.py --node t-beam-92 --info-file info.txt
    identity_guard.py --self-test

Exit status 0 = node fit for tests, 1 = refused (reasons printed), 2 = usage.
stdlib + pyserial (only for --port).
"""

from __future__ import annotations

import argparse
import json
import re
import socket
import sys
import time
from dataclasses import dataclass
from pathlib import Path

HERE = Path(__file__).resolve().parent
FLEET_FILE = HERE / "fleet.json"
TESTDATA = HERE / "testdata" / "identity"

PLACEHOLDER_CALLS = {"XX0XXX", ""}


class IdentityError(RuntimeError):
    """The node is not fit for tests; str() lists every reason."""


@dataclass
class NodeInfo:
    call: str | None
    txpower_dbm: int | None
    webserver: bool | None
    has_ip: bool | None
    ip: str | None
    clock_year: int | None
    clock_source: str | None


def parse_info(text: str) -> NodeInfo:
    """Pull the identity fields out of a ``--info`` reply (serial or 2323)."""
    text = text.replace("\r", "")

    def last(pattern: str) -> re.Match[str] | None:
        found = list(re.finditer(pattern, text))
        return found[-1] if found else None

    m_call = last(r"\.\.\.Call: <([^>]*)>")
    m_src = last(r"UTC-OFF [-0-9.]+ \[([^\]]*)\]")
    m_pwr = last(r"TXPWR (-?\d+) dBm")
    m_web = last(r"\.\.\.Webserver\s+(on|off)")
    m_hasip = last(r"\.\.\.hasIpAddress: (yes|no)")
    m_ip = last(r"\.\.\.IP address\s*: ([0-9.]+)")
    m_upd = last(r"\.\.\.UPDATE: (\d{4})-")
    return NodeInfo(
        call=m_call.group(1).strip() if m_call else None,
        txpower_dbm=int(m_pwr.group(1)) if m_pwr else None,
        webserver=(m_web.group(1) == "on") if m_web else None,
        has_ip=(m_hasip.group(1) == "yes") if m_hasip else None,
        ip=m_ip.group(1) if m_ip else None,
        clock_year=int(m_upd.group(1)) if m_upd else None,
        clock_source=m_src.group(1) if m_src else None,
    )


def load_fleet(path: Path = FLEET_FILE) -> dict:
    return json.loads(path.read_text())


def check(info: NodeInfo, node: str, fleet: dict) -> list[str]:
    """Every reason why ``node`` must not be tested; empty list = fit."""
    reasons: list[str] = []
    entry = fleet["nodes"].get(node)
    if entry is None:
        return [f"node '{node}' is not registered in fleet.json"]

    base = (info.call or "").split("-")[0]
    if info.call is None:
        reasons.append("no '...Call: <...>' line in --info (no reply, or wrong port)")
    elif base in PLACEHOLDER_CALLS:
        reasons.append(f"callsign is the placeholder '{info.call}': the node never transmits")
    elif info.call != entry["call"]:
        reasons.append(f"callsign is {info.call}, fleet.json says {entry['call']} for {node}")

    limit = fleet["max_txpower_dbm"]
    if info.txpower_dbm is None:
        reasons.append("no TXPWR in --info")
    elif info.txpower_dbm > limit:
        reasons.append(f"TX power {info.txpower_dbm} dBm above the bench limit of {limit} dBm")

    if entry["family"] == "esp32":
        if not info.has_ip:
            reasons.append("no WiFi IP (hasIpAddress not yes): no NTP, no web server, no net console")
        if not info.webserver:
            reasons.append("web server off")
        if info.clock_year is None or info.clock_year < 2025:
            reasons.append(f"clock not set (UPDATE year {info.clock_year}, source {info.clock_source})")
    return reasons


def require(info_text: str, node: str, fleet: dict | None = None) -> NodeInfo:
    """Raise IdentityError unless ``node`` is fit for tests."""
    info = parse_info(info_text)
    reasons = check(info, node, fleet if fleet is not None else load_fleet())
    if reasons:
        raise IdentityError(f"{node}: refused -- " + "; ".join(reasons))
    return info


def require_node(node: str, port: str | None = None, fleet: dict | None = None) -> NodeInfo:
    """Read ``--info`` from ``node`` and raise IdentityError unless it is fit.

    For harnesses that do not speak the text console themselves (BLE): uses
    ``port`` (serial) when given, else the node's registered net-console host.
    """
    fleet = fleet if fleet is not None else load_fleet()
    entry = fleet["nodes"].get(node)
    if entry is None:
        raise IdentityError(f"{node}: refused -- node '{node}' is not registered in fleet.json")
    if port:
        text = read_info_serial(port)
    elif entry.get("host"):
        text = read_info_net(entry["host"])
    else:
        raise IdentityError(f"{node}: refused -- no console path: pass --guard-port (fleet.json has no host)")
    return require(text, node, fleet)


def add_guard_args(ap: argparse.ArgumentParser) -> None:
    """The standard --node / --guard-port / --no-identity-guard arguments."""
    ap.add_argument("--node", default=None, help="fleet.json node name (identity guard, mandatory)")
    ap.add_argument("--guard-port", default=None, help="serial port for the identity check (default: fleet host, 2323)")
    ap.add_argument("--no-identity-guard", action="store_true", help="skip the guard -- only for setting up a node's identity")


def enforce(ap: argparse.ArgumentParser, args: argparse.Namespace) -> None:
    """Exit via ap.error()/SystemExit unless the node passes (or the guard is waived)."""
    if args.no_identity_guard:
        return
    if not args.node:
        ap.error("--node NAME is required (fleet.json) unless --no-identity-guard is given for identity setup")
    try:
        info = require_node(args.node, args.guard_port)
    except (IdentityError, OSError) as exc:
        print(exc, file=sys.stderr)
        raise SystemExit(1)
    print(f"identity guard: {args.node} ok -- {info.call}, {info.txpower_dbm} dBm")


def fleet_problems(fleet: dict) -> list[str]:
    """Registry consistency: bench SSIDs unique and recorded as taken."""
    problems: list[str] = []
    seen: dict[str, str] = {}
    for name, entry in fleet["nodes"].items():
        call = entry["call"]
        if call.split("-")[0] in PLACEHOLDER_CALLS:
            problems.append(f"{name}: placeholder callsign {call}")
        if call in seen:
            problems.append(f"{name} and {seen[call]} share {call}")
        seen[call] = name
        ssid = call.rsplit("-", 1)[-1] if "-" in call else ""
        if ssid not in fleet["taken_ssids"]:
            problems.append(f"{name}: SSID {ssid} missing from taken_ssids")
    return problems


def read_info_net(host: str, timeout: float = 4.0) -> str:
    s = socket.create_connection((host, 2323), timeout=10)
    s.settimeout(0.3)
    out = b""
    end = time.time() + 1.5
    while time.time() < end:
        try:
            out += s.recv(4096)
        except socket.timeout:
            pass
    s.sendall(b"--info\r\n")
    end = time.time() + timeout
    while time.time() < end:
        try:
            d = s.recv(4096)
            if not d:
                break
            out += d
        except socket.timeout:
            pass
    s.close()
    return out.decode(errors="replace")


def read_info_serial(port: str, timeout: float = 5.0) -> str:
    import serial  # pyserial, only needed here

    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 0.2
    # DTR on: nRF52/S3 native USB stay mute without it (see serial_session notes).
    s.dtr, s.rts = True, False
    s.open()
    out = b""
    end = time.time() + 1.5
    while time.time() < end:
        out += s.read(4096)
    for ch in b"--info":
        s.write(bytes([ch]))
        time.sleep(0.02)
    s.write(b"\n")
    end = time.time() + timeout
    while time.time() < end:
        out += s.read(4096)
    s.close()
    return out.decode(errors="replace")


def self_test() -> int:
    fleet = load_fleet()
    failures: list[str] = []

    def expect(label: str, got: object, want: object) -> None:
        if got != want:
            failures.append(f"{label}: got {got!r}, want {want!r}")

    expect("fleet consistent", fleet_problems(fleet), [])

    bad = (TESTDATA / "tbeam_xx0xxx_info.txt").read_text()
    info = parse_info(bad)
    expect("placeholder call parsed", info.call, "XX0XXX-00")
    reasons = check(info, "t-beam-92", fleet)
    expect("placeholder refused", any("placeholder" in r for r in reasons), True)
    expect("20 dBm refused", any("TX power 20 dBm" in r for r in reasons), True)
    expect("no WiFi refused", any("no WiFi IP" in r for r in reasons), True)
    try:
        require(bad, "t-beam-92", fleet)
        failures.append("require() accepted an XX0XXX node")
    except IdentityError:
        pass

    good = (TESTDATA / "tbeam_ok_info.txt").read_text()
    expect("configured T-Beam fit", check(parse_info(good), "t-beam-92", fleet), [])
    expect(
        "right node, wrong registry entry",
        check(parse_info(good), "t-deck-14", fleet),
        ["callsign is DK5EN-92, fleet.json says DK5EN-14 for t-deck-14"],
    )

    rak = (TESTDATA / "rak_ok_info.txt").read_text()
    expect("nRF52 fit without WiFi", check(parse_info(rak), "rak-90", fleet), [])
    expect("unregistered node", check(parse_info(rak), "heltec-7", fleet), ["node 'heltec-7' is not registered in fleet.json"])

    dup = json.loads(json.dumps(fleet))
    dup["nodes"]["t-deck-14"]["call"] = "DK5EN-92"
    expect("duplicate SSID caught", any("share DK5EN-92" in p for p in fleet_problems(dup)), True)

    if failures:
        print(f"SELF-TEST FAILED ({len(failures)}):")
        for f in failures:
            print(f"  - {f}")
        return 1
    print("self-test passed")
    return 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="refuse tests on nodes without a valid identity")
    ap.add_argument("--node", help="fleet.json node name, e.g. t-beam-92")
    src = ap.add_mutually_exclusive_group()
    src.add_argument("--host", help="read --info over the 2323 net console")
    src.add_argument("--port", help="read --info over serial (DTR on)")
    src.add_argument("--info-file", type=Path, help="check a saved --info reply")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args(argv)
    if a.self_test:
        return self_test()
    if not a.node or not (a.host or a.port or a.info_file):
        ap.error("--node and one of --host/--port/--info-file are required")
    fleet = load_fleet()
    problems = fleet_problems(fleet)
    if problems:
        print("fleet.json inconsistent: " + "; ".join(problems))
        return 1
    if a.host:
        text = read_info_net(a.host)
    elif a.port:
        text = read_info_serial(a.port)
    else:
        text = a.info_file.read_text()
    try:
        info = require(text, a.node, fleet)
    except IdentityError as exc:
        print(exc)
        return 1
    print(
        f"{a.node}: ok -- {info.call}, {info.txpower_dbm} dBm, "
        f"ip {info.ip or '-'}, web {'on' if info.webserver else 'off'}, clock {info.clock_source}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
