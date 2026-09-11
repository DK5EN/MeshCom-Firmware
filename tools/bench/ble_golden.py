#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["bleak>=0.22"]
# ///
"""BLE golden-capture client for the MeshCom bench (test plan P0.8 / step H4).

Connects to a node exactly as the phone app does -- Nordic UART Service, same
frame shapes, same opcodes -- replays a fixed write corpus and records every
notification. This is the only instrument that can capture the BLE wire surface
before and after the refactor; the app itself is not scriptable.

Run it with uv so the dependency is not installed into the PlatformIO venv:

    uv run --with bleak tools/bench/ble_golden.py --name DK5EN-93 \\
        --corpus test/golden/corpus/ble/writes.txt --out test/golden/hw/G0/heltec-93/
    uv run --with bleak tools/bench/ble_golden.py --name DK5EN-90 --pin 100000 ...
    uv run --with bleak tools/bench/ble_golden.py --scan
    python3 tools/bench/ble_golden.py --self-test        # frame builders only, no BLE

The link, as both ends implement it (doc 11 §4, `src/phone_commands.cpp`,
`src/nrf52/nrf52_ble.cpp`):

    write   [len][opcode][payload...]     len = total frame length
    notify  [flag][type][payload...]      flag 0x40 text/pos, 0x44 JSON, 0x91 mheard

Opcodes the app actually sends, and the only ones driven here. The firmware
accepts 0x50..0xF0 as well, but those write settings -- driving them from a
capture would reconfigure the node mid-run.

    0x10  hello     `04 10 20 30`, or `23 10 20 30 <32 B sha256("%06u" % pin)>`
    0x20  timesync  `06 20 <int32 LE unix seconds>`
    0xA0  text      `<n+2> A0 <utf-8 bytes>`

**The PIN is a credential and is never written to the capture.** Pass it on the
command line or via `--pin-file`; the recorded hello frame has its 32 hash
bytes replaced by a marker, so the capture stays committable. Whether a PIN was
used at all is recorded, because that changes the node's answer.

**Timesync uses a fixed timestamp from the corpus, not `now`.** A wall-clock
value would differ between the before and after run and every downstream
timestamp with it.

Practical constraints on the Mac: the node holds **one** connection, so the
phone must be disconnected first; macOS handles passkey pairing with a system
dialog once per node, and a firmware erase invalidates the bond, which then
has to be removed from the Mac's Bluetooth list.
"""

from __future__ import annotations

import argparse
import asyncio
import hashlib
import struct
import sys
import time
from pathlib import Path
from typing import Iterable, List, Optional, Tuple

NUS_SERVICE = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
NUS_TX_CHAR = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"   # phone -> node (write)
NUS_RX_CHAR = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"   # node -> phone (notify)

OP_HELLO = 0x10
OP_TIMESYNC = 0x20
OP_TEXT = 0xA0

# doc 11 §4.3 / phone_commands.cpp:47-70
NOTIFY_FLAGS = {0x40: "text/pos", 0x44: "json", 0x91: "mheard", 0x41: "ack"}

# A fixed instant, so before and after runs agree: 2026-01-01 00:00:00 UTC.
CORPUS_TIMESTAMP = 1767225600

PIN_MARKER = b"<pin-hash-32>"


# --------------------------------------------------------------- frames


def build_hello(pin: Optional[int]) -> bytes:
    """`04 10 20 30`, or the 35-byte authenticated form.

    The hash is over the PIN rendered as a zero-padded six-digit *string*, not
    over the number -- `hash_pin()` in `phone_commands.cpp:228` formats `%06u`
    and hashes exactly six bytes.
    """
    if pin is None:
        return bytes([0x04, OP_HELLO, 0x20, 0x30])
    digest = hashlib.sha256(f"{pin:06d}".encode("ascii")).digest()
    return bytes([0x23, OP_HELLO, 0x20, 0x30]) + digest


def build_timesync(epoch: int = CORPUS_TIMESTAMP) -> bytes:
    return bytes([0x06, OP_TIMESYNC]) + struct.pack("<i", epoch)


def build_text(text: str) -> bytes:
    payload = text.encode("utf-8")
    return bytes([len(payload) + 2, OP_TEXT]) + payload


def redact(frame: bytes) -> bytes:
    """Strip the PIN hash out of a hello frame before it is written down."""
    if len(frame) >= 36 and frame[1] == OP_HELLO and frame[0] == 0x23:
        return frame[:4] + PIN_MARKER
    return frame


def decode_notify(frame: bytes) -> str:
    if not frame:
        return "empty"
    flag = frame[0]
    name = NOTIFY_FLAGS.get(flag, f"0x{flag:02x}")
    if flag == 0x44 and len(frame) > 1:
        return f"{name}/{chr(frame[1]) if 32 <= frame[1] < 127 else frame[1]}"
    if flag == 0x40 and len(frame) > 1:
        return f"{name}/{chr(frame[1]) if 32 <= frame[1] < 127 else frame[1]}"
    return name


# --------------------------------------------------------------- corpus


def read_corpus(path: Path) -> List[str]:
    """One write per line; '#' comments and blank lines ignored."""
    return [
        line.strip()
        for line in path.read_text().splitlines()
        if line.strip() and not line.strip().startswith("#")
    ]


# --------------------------------------------------------------- capture


class Capture:
    def __init__(self) -> None:
        self.t0 = time.monotonic()
        self.notifications: List[Tuple[float, bytes]] = []
        self.writes: List[Tuple[float, bytes, str]] = []

    def on_notify(self, _sender, data: bytearray) -> None:
        self.notifications.append((time.monotonic() - self.t0, bytes(data)))

    def on_write(self, frame: bytes, label: str) -> None:
        self.writes.append((time.monotonic() - self.t0, redact(frame), label))

    def write_files(self, out: Path) -> List[Path]:
        out.mkdir(parents=True, exist_ok=True)
        blob = bytearray()
        lines = []
        for t, data in self.notifications:
            blob += struct.pack("<H", len(data)) + data
            lines.append(f"{t:9.3f} len={len(data):3d} {decode_notify(data):12} {data.hex()}")
        written = [out / "ble-frames.bin", out / "ble-frames.txt", out / "ble-writes.txt"]
        written[0].write_bytes(bytes(blob))
        written[1].write_text("\n".join(lines) + ("\n" if lines else ""))
        written[2].write_text(
            "".join(
                f"{t:9.3f} {label:24} {frame.hex() if isinstance(frame, bytes) else frame}\n"
                for t, frame, label in self.writes
            )
        )
        return written


# --------------------------------------------------------------- run


PERMISSION_HELP = """
macOS denied Bluetooth access to the application this shell runs under.
CoreBluetooth grants the permission per *application*, not per script, so the
host terminal is what has to be allowed -- here that is Terminal.app, not
python and not uv.

  System Settings -> Privacy & Security -> Bluetooth -> enable Terminal

If Terminal is not listed, the prompt was dismissed once and macOS will not
ask again; quit Terminal completely (Cmd-Q, not just the window) and start it
again, then re-run this command so the prompt reappears.
""".strip()


async def run(args: argparse.Namespace) -> int:
    from bleak import BleakClient, BleakScanner

    if args.scan:
        devices = await BleakScanner.discover(timeout=args.scan_seconds,
                                              service_uuids=[NUS_SERVICE])
        for d in devices:
            print(f"{d.address}  {d.name}")
        if not devices:
            print("no NUS device found; is the phone still connected to the node?",
                  file=sys.stderr)
        return 0

    target = args.address
    if target is None:
        device = await BleakScanner.find_device_by_filter(
            lambda d, ad: (d.name or "").upper().find(args.name.upper()) >= 0,
            timeout=args.scan_seconds,
        )
        if device is None:
            print(f"node {args.name!r} not found in {args.scan_seconds:.0f} s; "
                  f"the node holds one connection -- disconnect the phone first",
                  file=sys.stderr)
            return 1
        target = device.address
        print(f"found {args.name} at {target}", file=sys.stderr)

    cap = Capture()
    corpus = read_corpus(args.corpus) if args.corpus else []

    async with BleakClient(target) as client:
        await client.start_notify(NUS_RX_CHAR, cap.on_notify)
        # INIT_CONN_WAIT: the app waits before its hello and the node needs it.
        await asyncio.sleep(args.connect_wait)

        async def send(frame: bytes, label: str) -> None:
            await client.write_gatt_char(NUS_TX_CHAR, frame, response=True)
            cap.on_write(frame, label)
            await asyncio.sleep(args.gap)

        await send(build_hello(args.pin), f"hello pin={'yes' if args.pin else 'no'}")
        await send(build_timesync(args.timestamp), f"timesync {args.timestamp}")
        for line in corpus:
            await send(build_text(line), f"text {line[:18]!r}")

        await asyncio.sleep(args.listen)
        await client.stop_notify(NUS_RX_CHAR)

    for path in cap.write_files(args.out):
        print(f"wrote {path}", file=sys.stderr)
    print(f"{len(cap.notifications)} notifications, {len(cap.writes)} writes",
          file=sys.stderr)
    return 0


# --------------------------------------------------------------- self-test


def _self_test() -> int:
    failures = 0

    if build_hello(None) != bytes.fromhex("04102030"):
        failures += 1
        print("FAIL: open hello frame")

    h = build_hello(100000)
    if len(h) != 36 or h[0] != 0x23 or h[1] != OP_HELLO:
        failures += 1
        print(f"FAIL: authenticated hello header: {h[:4].hex()}")
    # The firmware hashes the zero-padded six-digit STRING, not the integer.
    if h[4:] != hashlib.sha256(b"100000").digest():
        failures += 1
        print("FAIL: PIN hash is not sha256 of the %06u string")
    if build_hello(1)[4:] != hashlib.sha256(b"000001").digest():
        failures += 1
        print("FAIL: PIN is not zero-padded to six digits")

    if build_timesync(1767225600) != bytes.fromhex("0620") + struct.pack("<i", 1767225600):
        failures += 1
        print("FAIL: timesync frame")

    t = build_text("--info")
    if t[0] != 8 or t[1] != OP_TEXT or t[2:] != b"--info":
        failures += 1
        print(f"FAIL: text frame: {t.hex()}")
    # Length is the TOTAL frame length, payload + 2.
    if build_text("")[0] != 2:
        failures += 1
        print("FAIL: empty text frame length")
    umlaut = build_text("grüß")
    if umlaut[0] != len("grüß".encode("utf-8")) + 2:
        failures += 1
        print("FAIL: length counts utf-8 bytes, not characters")

    r = redact(build_hello(100000))
    if r != bytes.fromhex("23102030") + PIN_MARKER:
        failures += 1
        print("FAIL: the PIN hash survived redaction")
    if redact(build_hello(None)) != build_hello(None):
        failures += 1
        print("FAIL: redact altered an open hello")

    if decode_notify(bytes([0x44, ord("I")])) != "json/I":
        failures += 1
        print("FAIL: notify decode")

    print("ble_golden.py self-test: " + ("ok" if failures == 0 else f"{failures} failure(s)"))
    return 1 if failures else 0


async def run_guarded(args: argparse.Namespace) -> int:
    """run(), with the one failure that is a setup step rather than a bug."""
    try:
        return await run(args)
    except Exception as exc:                      # bleak's own class, imported late
        if type(exc).__name__ == "BleakBluetoothNotAvailableError":
            print(f"{exc}\n\n{PERMISSION_HELP}", file=sys.stderr)
            return 2
        raise


def main(argv: Iterable[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--name", default=None, help="node name to scan for, e.g. DK5EN-93")
    ap.add_argument("--address", default=None, help="BLE address, skips the scan")
    ap.add_argument("--pin", type=int, default=None,
                    help="six-digit app PIN; required when the node's bt_code > 0")
    ap.add_argument("--pin-file", type=Path, default=None,
                    help="read the PIN from a file instead of the command line")
    ap.add_argument("--corpus", type=Path, default=None, help="write corpus, one line per write")
    ap.add_argument("--out", type=Path, default=Path("."), help="capture directory")
    ap.add_argument("--timestamp", type=int, default=CORPUS_TIMESTAMP,
                    help="fixed unix time for the timesync frame")
    ap.add_argument("--gap", type=float, default=0.5, help="seconds between writes")
    ap.add_argument("--connect-wait", type=float, default=2.0,
                    help="seconds after subscribe before the hello")
    ap.add_argument("--listen", type=float, default=10.0,
                    help="seconds to keep recording after the last write")
    ap.add_argument("--scan", action="store_true", help="list NUS devices and exit")
    ap.add_argument("--scan-seconds", type=float, default=10.0)
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args(list(argv) if argv is not None else None)

    if args.self_test:
        return _self_test()
    if args.pin_file is not None:
        args.pin = int(args.pin_file.read_text().strip())
    if not args.scan and not (args.name or args.address):
        ap.error("pass --name or --address, or --scan")

    return asyncio.run(run_guarded(args))


if __name__ == "__main__":
    raise SystemExit(main())
