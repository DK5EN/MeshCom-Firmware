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
    python3 tools/bench/ble_golden.py --compare G0/heltec-93/ble-frames.bin \\
                                                G1/heltec-93/ble-frames.bin

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

Discovery is by **advertised name**, not by service UUID: a MeshCom node
advertises as `MC-<id>-<callsign>` and does not put the NUS UUID in the
advertisement at all (nRF52 advertises the settings service 0xF0A0, the ESP32
manufacturer data with the scan response off). NUS only appears after
connecting.

Practical constraints on the Mac: the node holds **one** connection, so the
phone must be disconnected first; macOS handles passkey pairing with a system
dialog once per node, and a firmware erase invalidates the bond, which then
has to be removed from the Mac's Bluetooth list.
"""

from __future__ import annotations

import argparse
import asyncio
import hashlib
import re
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
MSG_TYPE_ACK = 0x41   # doc 11 §1.5, the 12-byte binary ack

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


def own_source(frame: bytes, own_calls: Tuple[str, ...]) -> Optional[str]:
    """Source path of a 0x40 notification, or None if it is not one.

    A 0x40 notification is the flag byte followed by the LoRa frame itself, so
    the frame parser reads it directly. `response` is the pseudo-source the
    firmware uses for a command answer.
    """
    if not frame or frame[0] != 0x40:
        return None
    sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "test" / "golden"))
    import mc_frame
    parsed = mc_frame.parse(frame[1:])
    return ",".join(parsed.path) if parsed else None


# JSON registers whose payload is live state, not configuration: they cannot
# byte-compare between two runs on the same firmware, let alone across a
# refactor. MH is the mheard table (changes with every frame heard), W the
# sensor readings, G the GPS fix, satellite count and distance. They are
# recorded but kept out of the compared artifact.
#
# This is a stopgap, not the end state: the right answer is to normalize the
# *values* of the volatile keys and still compare the key set and order, the
# same way `test/golden/normalize.py` treats console text. Until that exists,
# excluding three registers is honest and a value-masking comparison is not.
VOLATILE_REGISTERS = ("MH",)

# Keys whose *value* is a live reading. The key must stay in the comparison --
# a register that stops emitting a field is a regression -- but the value
# cannot: it changes between two runs of the same firmware. Measured on the
# bench: `I` differs only in `BATV` (4.273120605 vs 4.266834961), which is why
# excluding the whole `I` register would have been the wrong call.
#
# LAT/LON are masked deliberately and at a cost: a GPS regression would not
# show here. It has to be caught by the `--pos` console golden instead, where
# the value is not a moving fix.
VOLATILE_JSON_KEYS = (
    "BATV", "BATP",                                        # battery ADC
    "TEMP", "HUM", "PRES", "QNH", "GAS", "CO2",            # weather sensors
    "TOFFI", "TOUT", "TOFFO", "VBUS", "VSHUNT", "VAMP", "VPOW",
    "LAT", "LON", "ALT", "SAT", "SFIX", "HDOP", "RATE",    # GPS fix
    "NEXT", "DIST", "DIRn", "DIRo",
    "DATE", "TIME",                                        # wall clock
)

_VOLATILE_VALUE_RE = re.compile(
    rb'("(?:' + b"|".join(k.encode() for k in VOLATILE_JSON_KEYS) + rb')"\s*:\s*)'
    rb'(?:"[^"]*"|-?[0-9][0-9.eE+-]*|true|false|null)'
)


def mask_json_values(payload: bytes) -> bytes:
    """Replace the values of live-reading keys, keep the keys and their order."""
    return _VOLATILE_VALUE_RE.sub(rb'\1<v>', payload)

_TYP_RE = re.compile(rb'"TYP"\s*:\s*"([A-Z0-9]+)"')


def register_of(frame: bytes) -> Optional[str]:
    """The TYP of a 0x44 JSON notification."""
    if not frame or frame[0] != 0x44:
        return None
    m = _TYP_RE.search(frame[1:])
    return m.group(1).decode("ascii") if m else None


def classify(frame: bytes, own_calls: Tuple[str, ...]) -> str:
    """`mesh` for traffic the node received off the air, `local` otherwise.

    This is the difference between a capture that can be compared and one that
    cannot. A bench node is on a live mesh: in the very first run, 2 of 24
    notifications were inbound frames from DO8AIL-1 and DL2YED-96 that have
    nothing to do with the corpus and will never repeat. They have to be
    recorded -- dropping them would hide a real regression in the RX-to-phone
    path -- but they are excluded from the byte comparison.
    """
    if register_of(frame) in VOLATILE_REGISTERS:
        return "volatile"
    # The compact binary ack (doc 11 §1.5) is asynchronous: it arrives when a
    # peer or a gateway acknowledges, which depends on the mesh and not on any
    # write of ours. Seen landing inside a reply window in one run and after
    # the last write in the next, which shifted the whole compared sequence.
    if len(frame) > 1 and frame[0] == 0x40 and frame[1] == MSG_TYPE_ACK:
        return "volatile"
    src = own_source(frame, own_calls)
    if src is None:
        return "local"
    # The **originator** decides, not any hop. A relayed frame whose path is
    # "DM6CS-12,DF2SI-12,DK5EN-98" passed through one of our nodes but did not
    # come from one, and it is inbound mesh traffic. Matching any hop counted
    # exactly that frame as ours in a first run and shifted the whole compared
    # sequence by one against the next run; with the originator rule the two
    # runs are identical over all 22 shared frames. Doc 11 §1.4: originator
    # first in the source path.
    originator = src.split(",")[0]
    if originator == "response":
        return "local"
    if any(originator.upper().startswith(c.upper()) for c in own_calls):
        return "local"
    return "mesh"


class Capture:
    def __init__(self, own_calls: Tuple[str, ...] = ()) -> None:
        self.t0 = time.monotonic()
        self.own_calls = own_calls
        self.notifications: List[Tuple[float, bytes]] = []
        self.writes: List[Tuple[float, bytes, str]] = []
        self.corpus_start: Optional[float] = None   # set when the burst has settled

    def on_notify(self, _sender, data: bytearray) -> None:
        self.notifications.append((time.monotonic() - self.t0, bytes(data)))

    def on_write(self, frame: bytes, label: str) -> None:
        self.writes.append((time.monotonic() - self.t0, redact(frame), label))

    def _row(self, t: float, data: bytes, kind: str, attributed: str = "") -> str:
        src = own_source(data, self.own_calls)
        tag = f" src={src}" if src else ""
        via = f" <- {attributed}" if attributed else ""
        return (f"{t:9.3f} {kind:11} len={len(data):3d} "
                f"{decode_notify(data):12}{tag}{via} {data.hex()}")

    def attribute(self, t: float, window: float) -> Tuple[str, str]:
        """(class, the write this answers) for a notification at time `t`.

        A reply follows its write closely -- measured 0.2 to 0.4 s on both
        bench nodes, against a 0.6 s write gap. Anything that arrives later
        than `window` after the most recent write answers nothing: it is the
        node talking on its own (an mheard update triggered by mesh traffic, a
        periodic status frame), and it will not repeat in the next run. Such
        frames are recorded but kept out of the compared artifact.
        """
        preceding = [w for w in self.writes if w[0] <= t]
        if not preceding:
            return "spontaneous", ""
        wt, _frame, label = preceding[-1]
        if t - wt > window:
            return "spontaneous", ""
        return "reply", label

    def write_files(self, out: Path, window: float = 0.55) -> List[Path]:
        """Four files, split so the comparison has something it can compare.

        `ble-burst.txt` holds whatever arrived before the corpus started --
        spontaneous status frames, or mesh traffic during the settle. None of
        it can be attributed to a write, so none of it is compared.
        `ble-frames.txt` holds everything after the settle, each row tagged
        `reply` (with the write it answers), `mesh` or `spontaneous`. The
        `.bin` carries the `reply` frames only -- those are what the byte
        comparison runs on, and they are the only ones that repeat.
        """
        out.mkdir(parents=True, exist_ok=True)
        start = self.corpus_start if self.corpus_start is not None else 0.0

        burst, compared, excluded = [], [], []
        blob = bytearray()
        for t, data in self.notifications:
            kind = classify(data, self.own_calls)
            if t < start:
                burst.append(self._row(t, data, kind))
                continue
            when, label = self.attribute(t, window)
            if kind in ("mesh", "volatile"):
                excluded.append(self._row(t, data, kind))
            elif when == "spontaneous":
                excluded.append(self._row(t, data, "spontaneous"))
            else:
                compared.append(self._row(t, data, "reply", label))
                blob += struct.pack("<H", len(data)) + data

        written = [
            out / "ble-frames.bin", out / "ble-frames.txt",
            out / "ble-burst.txt", out / "ble-writes.txt",
        ]
        written[0].write_bytes(bytes(blob))
        written[1].write_text(
            "\n".join(compared)
            + ("\n" if compared else "")
            + ("\n# excluded from the comparison: inbound mesh traffic, frames the\n"
               "# node sent on its own, and the live-state registers MH/W/G\n"
               if excluded else "")
            + "\n".join(excluded) + ("\n" if excluded else "")
        )
        written[2].write_text("\n".join(burst) + ("\n" if burst else ""))
        written[3].write_text(
            "".join(
                f"{t:9.3f} {label:24} {frame.hex() if isinstance(frame, bytes) else frame}\n"
                for t, frame, label in self.writes
            )
        )
        self.counts = (len(burst), len(compared), len(excluded))
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
        # Deliberately NOT filtered on the NUS service UUID. A MeshCom node
        # does not advertise it: the nRF52 puts the settings service 0xF0A0 in
        # the packet (`Bluefruit.Advertising.addService(sett_service)`,
        # nrf52_ble.cpp:165) and the ESP32 advertises manufacturer data with
        # the scan response disabled (esp32_main.cpp:1784-1800). NUS is only
        # discoverable after connecting. The test plan §12.5 says scanning by
        # the NUS UUID plus the name is enough; the UUID half of that is wrong
        # and finds nothing.
        found = await BleakScanner.discover(timeout=args.scan_seconds, return_adv=True)
        rows = [
            (ad.rssi, addr, d.name or ad.local_name or "")
            for addr, (d, ad) in found.items()
            if (d.name or ad.local_name or "").upper().startswith(args.prefix.upper())
        ]
        for rssi, addr, name in sorted(rows, reverse=True):
            print(f"{rssi:5} {addr}  {name}")
        if not rows:
            print(f"no device whose name starts with {args.prefix!r}. The node holds one "
                  f"connection -- disconnect the phone first.", file=sys.stderr)
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

    # The node under test is the only station whose frames it echoes as its
    # own; everything else it hands to the phone it heard off the air. A
    # prefix like "DK5EN" is too broad -- DK5EN-98 is ours but is not this
    # node, and its position frames shifted a whole compared sequence by one.
    own = args.own_call or args.name or ""
    own_calls = tuple(c.strip() for c in own.split(",") if c.strip())
    cap = Capture(own_calls)
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

        # The node is not ready for text commands immediately after timesync.
        # Measured on DK5EN-93 (2026-09-11): with 0.6 s between timesync and
        # the first `--info`, that request produced no `I` register at all and
        # every later reply was attributed to the wrong request; with ~2.5 s,
        # all 13 registers answered in order. So wait a fixed minimum, then
        # until any spontaneous traffic has stopped. Anything that arrives
        # before the corpus starts goes to ble-burst.txt, not into the
        # comparison, because it cannot be attributed to a write.
        await asyncio.sleep(args.settle_min)
        settled = args.settle_min + await wait_quiet(
            cap, args.settle_quiet, args.settle_max)
        cap.corpus_start = time.monotonic() - cap.t0
        print(f"node settled after {settled:.1f} s, "
              f"{len(cap.notifications)} pre-corpus frames", file=sys.stderr)

        for line in corpus:
            await send(build_text(line), f"text {line[:18]!r}")

        await asyncio.sleep(args.listen)
        await client.stop_notify(NUS_RX_CHAR)

    for path in cap.write_files(args.out, args.reply_window):
        print(f"wrote {path}", file=sys.stderr)
    burst, compared, excluded = cap.counts
    print(f"{len(cap.writes)} writes; notifications: {burst} pre-corpus burst, "
          f"{compared} replies (compared), {excluded} mesh/spontaneous (excluded)",
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

    # Classification: an inbound frame is excluded, our own echo is not.
    sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "test" / "golden"))
    corpus_path = (Path(__file__).resolve().parents[2]
                   / "test" / "test_aprs_corpus" / "corpus.txt")
    frames = {}
    for line in corpus_path.read_text().splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            name, hexstr = line.split()
            frames[name] = bytes.fromhex(hexstr)
    # f001 path is DL2JA-1,DL2JA-2 -- foreign. f003 is DK5EN-90,DK5EN-91.
    if classify(b"\x40" + frames["f001"], ("DK5EN",)) != "mesh":
        failures += 1
        print("FAIL: a foreign-source notification was not classified as mesh")
    if classify(b"\x40" + frames["f003"], ("DK5EN",)) != "local":
        failures += 1
        print("FAIL: our own echo was classified as mesh")
    # f004 path is OE1XAR-33,DK5EN-98: relayed *through* one of ours, but the
    # originator is foreign, so it is inbound mesh traffic.
    if classify(b"\x40" + frames["f004"], ("DK5EN",)) != "mesh":
        failures += 1
        print("FAIL: a frame merely relayed through our node was counted as ours")
    # f010 path is DK5EN-90,DK5EN-98: originator ours.
    if classify(b"\x40" + frames["f010"], ("DK5EN",)) != "local":
        failures += 1
        print("FAIL: a frame originated by us was classified as mesh")
    if classify(bytes([0x44]) + b'{"TYP":"I"}', ("DK5EN",)) != "local":
        failures += 1
        print("FAIL: a JSON register reply was classified as mesh")
    for typ in VOLATILE_REGISTERS:
        if classify(bytes([0x44]) + b'{"TYP":"' + typ.encode() + b'","X":1}',
                    ("DK5EN",)) != "volatile":
            failures += 1
            print(f"FAIL: register {typ} was not classified as volatile")
    if classify(bytes([0x40, MSG_TYPE_ACK]) + b"\x01\x02\x03\x04", ("DK5EN",)) != "volatile":
        failures += 1
        print("FAIL: the binary ack was not classified as volatile")
    masked = mask_json_values(b'{"TYP":"I","CALL":"DK5EN-90","BATV":4.273120605,"HWID":9}')
    if masked != b'{"TYP":"I","CALL":"DK5EN-90","BATV":<v>,"HWID":9}':
        failures += 1
        print(f"FAIL: json masking: {masked!r}")
    if mask_json_values(b'{"TYP":"G","DATE":"2026-09-11 12:44:18"}') \
            != b'{"TYP":"G","DATE":<v>}':
        failures += 1
        print("FAIL: a string-valued volatile key was not masked")
    folded, dropped = collapse(["x", "x", "y", "x"])
    if folded != ["x", "y", "x"] or dropped != 1:
        failures += 1
        print(f"FAIL: collapse: {folded} {dropped}")

    if b"CALL" not in masked:
        failures += 1
        print("FAIL: masking removed a key instead of its value")

    if register_of(bytes([0x44]) + b'{"TYP":"CONFFIN"}') != "CONFFIN":
        failures += 1
        print("FAIL: register_of")
    if register_of(bytes([0x40, 0x3A])) is not None:
        failures += 1
        print("FAIL: register_of read a non-JSON frame")

    if decode_notify(bytes([0x44, ord("I")])) != "json/I":
        failures += 1
        print("FAIL: notify decode")

    print("ble_golden.py self-test: " + ("ok" if failures == 0 else f"{failures} failure(s)"))
    return 1 if failures else 0


# --------------------------------------------------------------- compare


def read_bin(path: Path) -> List[bytes]:
    data = path.read_bytes()
    out, i = [], 0
    while i + 2 <= len(data):
        (n,) = struct.unpack_from("<H", data, i)
        i += 2
        out.append(data[i:i + n])
        i += n
    return out


def signature(frame: bytes):
    """What must be equal between two runs of the same firmware.

    `msg_id` is excluded and nothing else is: it is minted from `millis()` and
    differs by construction every run (doc 11 §1.1). Everything the refactor
    could plausibly break -- flags, path, destination, payload, hw, mod, and
    the whole JSON body -- is compared.
    """
    if frame[:1] == b"\x44":
        return ("json", mask_json_values(frame[1:].rstrip(b"\x00")))
    sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "test" / "golden"))
    import mc_frame
    parsed = mc_frame.parse(frame[1:])
    if parsed is None:
        return ("raw", frame[0], len(frame))
    return ("frame", frame[1], parsed.flags, tuple(parsed.path),
            parsed.dest, parsed.payload, parsed.hw, parsed.mod)


def collapse(sigs: List) -> Tuple[List, int]:
    """Fold runs of identical consecutive signatures into one.

    A node echoes an outgoing message to the phone once or twice depending on
    whether it also hears its own transmission back -- measured on RAK-90:
    the same direct message echoed twice in one run and once in the next, and
    that alone shifted every following frame. The *content* of the echo is
    determined by what was written and is still compared; only the repeat
    count is dropped, because it is a property of the radio, not of the code
    under test.
    """
    out: List = []
    dropped = 0
    for sig in sigs:
        if out and sig == out[-1]:
            dropped += 1
            continue
        out.append(sig)
    return out, dropped


def compare_captures(a: Path, b: Path) -> int:
    """Diff two compared artifacts; 0 if they agree over their shared length.

    A run can end with one extra late reply inside the listen window, so a
    trailing surplus is reported but is not a failure; a difference inside the
    shared prefix is.
    """
    fa, fb = read_bin(a), read_bin(b)
    sa, na = collapse([signature(f) for f in fa])
    sb, nb = collapse([signature(f) for f in fb])
    if na or nb:
        print(f"collapsed {na} + {nb} repeated echo(es)")
    shared = min(len(sa), len(sb))
    bad = [i for i in range(shared) if sa[i] != sb[i]]
    print(f"{a}: {len(sa)} frames\n{b}: {len(sb)} frames")
    for i in bad:
        print(f"  differ at {i}:\n    a: {str(sa[i])[:160]}\n    b: {str(sb[i])[:160]}")
    if len(sa) != len(sb):
        print(f"  note: {abs(len(sa) - len(sb))} trailing frame(s) in the longer "
              f"capture, not a failure on their own")
    print("identical over the shared prefix" if not bad else f"{len(bad)} difference(s)")
    return 1 if bad else 0


async def wait_quiet(cap: "Capture", quiet: float, maximum: float) -> float:
    """Block until no notification has arrived for `quiet` seconds."""
    start = time.monotonic()
    last_count = -1
    last_change = start
    while time.monotonic() - start < maximum:
        if len(cap.notifications) != last_count:
            last_count = len(cap.notifications)
            last_change = time.monotonic()
        elif time.monotonic() - last_change >= quiet:
            break
        await asyncio.sleep(0.1)
    return time.monotonic() - start


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
    ap.add_argument("--gap", type=float, default=2.0,
                    help="seconds between writes. Measured reply latency on the bench "
                         "nodes reaches 1.6 s for the `--wrong command` path, so a gap "
                         "below ~1.8 s lets a reply fall into the next write's slot")
    ap.add_argument("--connect-wait", type=float, default=2.0,
                    help="seconds after subscribe before the hello")
    ap.add_argument("--listen", type=float, default=10.0,
                    help="seconds to keep recording after the last write")
    ap.add_argument("--scan", action="store_true", help="list NUS devices and exit")
    ap.add_argument("--scan-seconds", type=float, default=10.0)
    ap.add_argument("--own-call", default=None,
                    help="callsign(s) the node under test originates as; defaults to "
                         "--name. A 0x40 notification whose path ORIGINATOR is none of "
                         "them is inbound mesh traffic and is excluded")
    ap.add_argument("--reply-window", type=float, default=1.9,
                    help="a notification later than this after the most recent write "
                         "answers nothing and is excluded from the comparison")
    ap.add_argument("--settle-min", type=float, default=2.5,
                    help="fixed wait after timesync before the corpus starts; below "
                         "~2 s the node drops the first text command")
    ap.add_argument("--settle-quiet", type=float, default=1.5,
                    help="further seconds of silence required before the corpus starts")
    ap.add_argument("--settle-max", type=float, default=25.0,
                    help="give up waiting for the burst to settle after this")
    ap.add_argument("--prefix", default="MC-",
                    help="name prefix a MeshCom node advertises (default: MC-)")
    ap.add_argument("--compare", nargs=2, type=Path, metavar="BIN",
                    help="diff two ble-frames.bin captures (msg_id excluded)")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args(list(argv) if argv is not None else None)

    if args.self_test:
        return _self_test()
    if args.compare:
        return compare_captures(*args.compare)
    if args.pin_file is not None:
        args.pin = int(args.pin_file.read_text().strip())
    if not args.scan and not (args.name or args.address):
        ap.error("pass --name or --address, or --scan")

    return asyncio.run(run_guarded(args))


if __name__ == "__main__":
    raise SystemExit(main())
