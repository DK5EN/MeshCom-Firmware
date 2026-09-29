#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["bleak>=0.22"]
# ///
"""BLE stress client for the MeshCom bench: the phone app, but hostile.

Connects to a node over the Nordic UART Service exactly as the app does
(frame builders, NUS UUIDs, name-based scan, PIN hello and PIN redaction all
come from `ble_golden.py`) and drives five tests against the node's BLE link.
Every subcommand prints one line per event with a wall-clock stamp (so the
serial / net-console logs can be aligned), ends with one `PASS ...` / `FAIL ...`
summary line and exits 0 / 1.

    uv run --with bleak tools/bench/ble_stress.py burst --node rak-90 --cycles 5 --gap 8
    uv run --with bleak tools/bench/ble_stress.py pin --node t-beam-92 --expect-pin set --pin-file pin.txt
    uv run --with bleak tools/bench/ble_stress.py flood --node rak-90 --count 10
    uv run --with bleak tools/bench/ble_stress.py flood --node rak-90 --commands --rounds 30
    uv run --with bleak tools/bench/ble_stress.py settings-roundtrip --node rak-90
    uv run --with bleak tools/bench/ble_stress.py malformed --node rak-90
    python3 tools/bench/ble_stress.py --self-test        # builders + guards, no BLE

    burst               hello, collect the config burst until CONFFIN, check it
    pin                 PIN handling (the operator sets/clears the PIN on the node)
    flood               N group texts, or --commands: read-only commands in a tight loop
    settings-roundtrip  write back IDENTICAL values through one harmless opcode
    malformed           malformed phone->node frames, then check the link still answers

Bench rules this tool enforces itself, in code, not by convention
-----------------------------------------------------------------
* **The identity guard runs first** (`identity_guard.py`, operator rule
  2026-09-26), exactly as in `ble_golden.py`: `--node <fleet.json name>`
  is mandatory unless `--no-identity-guard`. It reads `--info` over the node's
  2323 net console or `--guard-port` serial, before any BLE traffic.
* **Every write goes through `guard_frame()`**, whichever subcommand builds
  it. Text is refused unless it is a `--` command from the read-only allowlist
  (`COMMAND_REPLIES`) or a message in the SINGLE-colon form `:{9}text` /
  `:{9999}text` / `:{DK5EN-nn}text`. Bare text (a broadcast), the two-colon
  form `::{9}text` (falls through to `*`: the 2026-09-11 incident) and every
  other destination raise `RefusedFrame`. Callsign / WiFi / position /
  altitude opcodes (0x50 0x55 0x70 0x80 0x90) are never sent; 0x95 only from
  `settings-roundtrip`; 0xF0 (save + reset) only with `--allow-save`.
* **The PIN is a credential.** It is read from `--pin` / `--pin-file`, never
  printed or written; hello frames are logged through `ble_golden.redact()`.
  The node's own PIN comes back in plain in the `I` register (`BPIN`) and the
  web password in `SN1` (`WSPWD`): JSON bodies are never printed, only TYP,
  size and key counts, and a diff prints those two values as `<redacted>`.

Expected burst (`burst`, and the baseline of every other subcommand)
--------------------------------------------------------------------
After a genuine hello the main loop runs `config_cmds[]` with BLE output on
(`src/esp32/esp32_main.cpp:312`, `src/nrf52/nrf52_main.cpp:300`)::

    --info --seset --wifiset --nodeset --wx --pos --aprsset --io --tel [--analogset]

then the MH list, then `--conffin` (the `!conffin_sent` branch of the
`isPhoneReady` block in `esp32_main.cpp` / `nrf52_main.cpp`). Several
commands emit two registers, so the 0x44 JSON frames that must arrive are
(producers in `src/command_functions.cpp`, line of the `"TYP"` assignment as
read on 2026-09-29; the tree drifts, grep `"TYP"] = "SN"` and friends)::

    I 6064   IS1 6092     --info       (IS1 was added after doc 11 s4.2 was written)
    SE 6355  S1 6377      --seset
    SW 6396  S2 6419      --wifiset
    SN 6539  SN1 6567     --nodeset    (sendNodeSetting)
    W 5896                --wx
    G 6478                --pos        (sendGpsJson)
    SA 6613               --aprsset    (sendAPRSset)
    IO 6001               --io
    TM 5868               --tel
    AN 6582               --analogset  ESP32 only, and even there `#ifndef
                                       BOARD_RAK4630`; expected only for
                                       --family esp32 (default: from fleet.json)
    MH*                   0..n neighbour records, newest first (mh_phone.cpp)
    CONFFIN 6630          --conffin, exactly once, last

Required fields per register are `REGISTER_FIELDS`, copied from the builders
(SN: command_functions.cpp:6539-6558, SN1: :6567-6571, ...). The builders run
through `bleJsonFrameFailSoft()`, which drops *trailing* fields of a document
over 244 characters -- so a missing field is a real finding (an overflow), not
noise. Frame limit: the 0x44 body is at most 245 bytes including the flag, i.e.
244 JSON characters (`BLE_JSON_PAYLOAD_MAX`, configuration_global.h:570).

`settings-roundtrip`: why 0x95
------------------------------
`readPhoneCommand()` (phone_commands.cpp) has six settings opcodes. Assessed by
what each does to a node that is handed back the value it already has:

* 0x50 callsign: renormalises, rewrites `node_short`, `iInitDisplay = 99`, on
  nRF52 renames the advertisement (reconnect). Side effects.
* 0x55 WiFi: the password never leaves the node (no register carries it), so
  "identical" cannot be written.
* 0x70 / 0x80 latitude / longitude: the node stores a double, the wire carries
  a float32; the round trip through float rounds the value (~1e-7 deg) and the
  N/S, E/W flag is rewritten. Not identical.
* 0x90 altitude: without the save flag it sets `posinfo_shot`/`pos_shot`/
  `wx_shot` -- the node puts a position on the AIR; with 0x0A it saves to flash.
* 0xF0 save + `delay(2000)` + reset. Only with `--allow-save`.
* **0x95 APRS symbol: two `char` assignments** (`node_symid`, `node_symcd`) and
  nothing else -- no save, no TX, no display, no reconnect, and the `SA`
  register carries both values as one-character strings, so an identical
  write is exactly representable. Chosen.

The message-id counter is not in any BLE register, but `--msgid` answers over
BLE (command_functions.cpp:1278, a `response` text frame), so the tool reads it
before and after and logs both next to `MARK before-write` / `MARK after-write`
lines the operator can align with a serial `--info`.
"""

from __future__ import annotations

import argparse
import asyncio
import contextlib
import json
import re
import struct
import sys
import time
from collections import Counter
from collections.abc import AsyncIterator, Callable, Iterable, Sequence
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Protocol

sys.path.insert(0, str(Path(__file__).resolve().parent))

from ble_cycle import stamp
from ble_golden import (
    NUS_RX_CHAR,
    NUS_TX_CHAR,
    OP_HELLO,
    OP_TEXT,
    OP_TIMESYNC,
    PERMISSION_HELP,
    PIN_MARKER,
    VOLATILE_JSON_KEYS,
    build_hello,
    build_text,
    build_timesync,
    decode_notify,
    redact,
    register_of,
    verify_no_broadcast,
)
from identity_guard import (
    add_guard_args,
    enforce,
    load_fleet,
)

Emit = Callable[[str], None]

OP_SYMBOL = 0x95
OP_SAVE = 0xF0
# Opcodes that write node settings and are never sent by this tool.
FORBIDDEN_OPS = frozenset({0x50, 0x55, 0x70, 0x80, 0x90})

BLE_JSON_PAYLOAD_MAX = 244  # configuration_global.h:570
TEXT_MAX_CHARS = 160  # sendMessage(): `{dst}text` longer than this is dropped
OVERSIZE_CHARS = 170  # payload characters of the deliberately oversized 0xA0
FLOOD_MAX = 200
DRAIN_PER_FRAME = 0.4  # seconds; the command ring drains one frame per 300 ms
DRAIN_BASE = 5.0
FLOOD_QUIET_MIN = 3.0
POLL = 0.02

# Registers of the config burst in the order config_cmds[] emits them.
BURST_ORDER = (
    "I",
    "IS1",
    "SE",
    "S1",
    "SW",
    "S2",
    "SN",
    "SN1",
    "W",
    "G",
    "SA",
    "IO",
    "TM",
    "AN",
)
ESP32_ONLY = frozenset({"AN"})


def _fields(names: str) -> tuple[str, ...]:
    return tuple(names.split())


# TYP -> fields the builder always writes (command_functions.cpp, line ranges
# in the comments). A field the document had to drop for size is missing on the
# wire and reported.
REGISTER_FIELDS: dict[str, tuple[str, ...]] = {
    # 6064-6083
    "I": _fields(
        "FWVER CALL ID HWID MAXV BLE BATP BATV GCB0 GCB1 GCB2 GCB3 GCB4 GCB5 "
        "CTRY BOOST BPIN"
    ),
    "IS1": _fields("BDATE"),  # 6092-6093
    "SE": _fields(  # 6355-6372
        "BME BMP BMP3 BMP3F AHT AHTF BMXF 680 680F 811 811F SS LPS33 OW OWPIN OWF "
        "USERPIN"
    ),
    "S1": _fields("INA226 SHUNT IMAX SAMP SHT SHTF 226 226F"),  # 6377-6385
    "SW": _fields("SSID IP GW AP DNS SUB"),  # 6396-6415
    "S2": _fields("OWNIP OWNGW OWNMS OWNDNS OWNNTP EUDP EUDPIP TXPOW"),  # 6419-6426
    "SN": _fields(  # 6539-6558, sendNodeSetting()
        "GW WS DISP BTN MSH GPS TRACK UTCOF TXP MQRG MSF MCR MBW GWNPOS NOALL "
        "NOPMOTHER BLED GWS"
    ),
    "SN1": _fields("VIA VIACALL WSPWD ASYM"),  # 6567-6571
    "W": _fields(  # 5896-5910
        "TEMP TOFFI TOUT TOFFO HUM PRES QNH ALT GAS CO2 VBUS VSHUNT VAMP VPOW"
    ),
    "G": _fields(  # 6478-6490, sendGpsJson()
        "LAT LON ALT SAT SFIX HDOP RATE NEXT DIST DIRn DIRo DATE"
    ),
    "SA": _fields("ATXT SYMID SYMCD NAME"),  # 6613-6617
    "IO": _fields("MCP23017 AxOUT AxVAL BxOUT BxVAL"),  # 6001-6006
    "TM": _fields("PARM UNIT FORMAT EQNS VALES PTIME"),  # 5868-5873
    "AN": _fields(  # 6582-6594, sendAnalogSetting()
        "APN AFC AK AFL ACK ADC ADCRAW ADCE1 ADCE2 ADCSL ADCOF ADCAT"
    ),
    "CONFFIN": (),  # 6630
}

# Read-only `--` commands the tool may send, and the JSON registers each one
# answers with (command_functions.cpp). Anything else starting with `--` is
# refused: a `--` string is dispatched by commandAction() and most of them
# change settings.
COMMAND_REPLIES: dict[str, tuple[str, ...]] = {
    "--info": ("I", "IS1"),
    "--seset": ("SE", "S1"),
    "--wifiset": ("SW", "S2"),
    "--nodeset": ("SN", "SN1"),
    "--wx": ("W",),
    "--pos": ("G",),
    "--aprsset": ("SA",),
    "--io": ("IO",),
    "--tel": ("TM",),
    "--conffin": ("CONFFIN",),
    "--msgid": (),  # answers with a text frame, see msgid_of()
}
DEFAULT_FLOOD_COMMANDS = ("--info", "--pos", "--seset")

SENSITIVE_KEYS = frozenset({"BPIN", "WSPWD"})


class RefusedFrame(Exception):
    """A frame the bench rules forbid; raised before anything reaches the air."""


@dataclass
class Verdict:
    ok: bool
    summary: str


def say(text: str) -> None:
    print(f"{stamp()}  {text}", flush=True)


# --------------------------------------------------------------- frames


def check_destination(dest: str) -> str:
    """Group 9, group 9999 or a DK5EN-nn direct contact; nothing else.

    The `{dst}` field of a text must close by index 10 (dst <= 9 characters,
    doc 11 s4.4), otherwise the firmware sends a broadcast with the braces
    left in the text. `DK5EN-` plus three characters is exactly 9.
    """
    d = dest.strip().upper()
    if d in ("9", "9999"):
        return d
    if re.fullmatch(r"DK5EN-[0-9A-Z]{1,3}", d):
        return d
    raise RefusedFrame(
        f"destination {dest!r} refused: only group 9, group 9999 or DK5EN-<ssid>"
    )


def build_group_text(
    dest: str, text: str, max_len: int | None = TEXT_MAX_CHARS
) -> bytes:
    """`:{dest}text` as an 0xA0 frame -- ONE colon.

    The firmware prepends one more colon (phone_commands.cpp case 0xA0, unless
    the text starts with `--`), giving the `::{dest}text` that sendMessage()
    parses. The two-colon form typed over BLE becomes `:::{dest}text`, parses
    no destination and goes out as a broadcast (2026-09-11, four bench nodes).
    """
    d = check_destination(dest)
    body = f"{{{d}}}{text}"
    if max_len is not None and len(body) > max_len:
        raise RefusedFrame(f"text of {len(body)} chars exceeds {max_len}")
    frame = build_text(":" + body)
    return frame


def build_command(cmd: str) -> bytes:
    if cmd not in COMMAND_REPLIES:
        raise RefusedFrame(f"command {cmd!r} is not on the read-only allowlist")
    return build_text(cmd)


def build_symbol(symid: str, symcd: str) -> bytes:
    """0x95: `04 95 <table> <symbol>`; conf_data[2], conf_data[3]."""
    return bytes([0x04, OP_SYMBOL, ord(symid), ord(symcd)])


def build_save() -> bytes:
    """0xF0: save settings to flash, wait 2 s, reset the node."""
    return bytes([0x02, OP_SAVE])


def check_text_payload(payload: bytes) -> None:
    try:
        text = payload.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise RefusedFrame("text payload is not UTF-8") from exc
    if text.startswith("--"):
        if text not in COMMAND_REPLIES:
            raise RefusedFrame(f"command {text!r} is not on the read-only allowlist")
        return
    if text.startswith("::"):
        raise RefusedFrame("two-colon form over BLE becomes a broadcast")
    m = re.match(r":\{([^}]*)\}", text)
    if m is None:
        raise RefusedFrame("bare text over BLE is a broadcast: use ':{9}text'")
    # judged exactly as the firmware will read it: no case or space folding
    if check_destination(m.group(1)) != m.group(1):
        raise RefusedFrame(f"destination {m.group(1)!r} is not in canonical form")


def guard_frame(
    frame: bytes,
    allow_settings: frozenset[int] = frozenset(),
    allow_save: bool = False,
) -> None:
    """Raise RefusedFrame unless the bench rules allow this write.

    Judged on the actual bytes (opcode at index 1, text from index 2), not on
    the declared length, so a malformed frame cannot smuggle a message past it.
    """
    if len(frame) < 2:
        raise RefusedFrame("frame shorter than length+opcode")
    op = frame[1]
    if op in (OP_HELLO, OP_TIMESYNC):
        return
    if op == OP_TEXT:
        check_text_payload(frame[2:])
        return
    if op == OP_SYMBOL:
        if OP_SYMBOL not in allow_settings:
            raise RefusedFrame("0x95 is only sent by settings-roundtrip")
        return
    if op == OP_SAVE:
        if not allow_save:
            raise RefusedFrame("0xF0 (save + reset) needs --allow-save")
        return
    if op in FORBIDDEN_OPS:
        raise RefusedFrame(f"settings opcode 0x{op:02X} is never sent")
    if op >= 0x50:
        raise RefusedFrame(f"opcode 0x{op:02X} may be a settings opcode: refused")
    # unknown low opcode: readPhoneCommand() has no case for it


def frame_repr(frame: bytes) -> str:
    """Hex for the log; a PIN hello keeps its header and loses its hash."""
    if redact(frame) != frame:
        return frame[:4].hex() + PIN_MARKER.decode("ascii")
    return frame.hex()


@dataclass(frozen=True)
class MalformedFrame:
    name: str
    frame: bytes
    note: str


def malformed_frames(group: str = "9") -> list[MalformedFrame]:
    """The malformed phone->node frames, with what the firmware should do.

    All text inside is either read-only (`--info`) or group 9 and longer than
    the 160-character limit (dropped by sendMessage() without transmitting).
    A truncated *PIN* hello is deliberately absent: on a node with a PIN it is
    a wrong hash and the firmware drops the link on purpose.
    """
    info = build_text("--info")  # 08 A0 '--info'
    over = build_group_text(group, "x" * OVERSIZE_CHARS, max_len=None)
    return [
        MalformedFrame(
            "len-too-big",
            bytes([0x40]) + info[1:],
            "declared 64, actual 8; msg_len-2 bytes are copied from a zeroed buffer",
        ),
        MalformedFrame(
            "len-zero", bytes([0x00]) + info[1:], "msg_len < 2: dropped, no reply"
        ),
        MalformedFrame(
            "len-one", bytes([0x01]) + info[1:], "msg_len < 2: dropped, no reply"
        ),
        MalformedFrame(
            "truncated-hello",
            bytes([0x04, OP_HELLO]),
            "magic bytes absent: ignored, no reply",
        ),
        MalformedFrame(
            "oversized-text",
            over,
            f"{len(over) - 2} payload bytes, over the {TEXT_MAX_CHARS}-char limit: "
            "dropped, group 9 only",
        ),
        MalformedFrame(
            "unknown-opcode", bytes([0x04, 0x33, 0x00, 0x00]), "no case: ignored"
        ),
    ]


# --------------------------------------------------------------- decoding


@dataclass
class JsonFrame:
    typ: str | None
    obj: dict[str, Any] | None
    json_len: int
    error: str | None


def json_frame(obj: dict[str, Any]) -> bytes:
    """A 0x44 notification as the node writes it: flag, JSON, two pad bytes."""
    return b"\x44" + json.dumps(obj, separators=(",", ":")).encode() + b"\x00\x00"


def decode_json_frame(frame: bytes) -> JsonFrame:
    body = frame[1:].rstrip(b"\x00")
    try:
        obj = json.loads(body.decode("utf-8"))
    except (UnicodeDecodeError, ValueError) as exc:
        return JsonFrame(None, None, len(body), f"{type(exc).__name__}: {exc}")
    if not isinstance(obj, dict):
        return JsonFrame(None, None, len(body), "JSON is not an object")
    typ = obj.get("TYP")
    return JsonFrame(typ if isinstance(typ, str) else None, obj, len(body), None)


def describe_rx(frame: bytes) -> str:
    if frame[:1] == b"\x44":
        info = decode_json_frame(frame)
        if info.obj is None:
            return f"json BAD {info.error} len={len(frame)}"
        over = (
            f" OVERSIZE({info.json_len}>{BLE_JSON_PAYLOAD_MAX})"
            if info.json_len > BLE_JSON_PAYLOAD_MAX
            else ""
        )
        return (
            f"json TYP={info.typ} json_len={info.json_len} keys={len(info.obj)}{over}"
        )
    return f"{decode_notify(frame)} len={len(frame)}"


def redact_obj(obj: dict[str, Any]) -> dict[str, Any]:
    return {k: ("<redacted>" if k in SENSITIVE_KEYS else v) for k, v in obj.items()}


def msgid_of(frame: bytes) -> int | None:
    """The counter out of a `--msgid` reply (a 0x40 `response` text frame)."""
    if frame[:1] != b"\x40":
        return None
    m = re.search(rb"--msgid (-?\d+)", frame)
    return int(m.group(1)) if m else None


def expected_typs(family: str) -> list[str]:
    return [t for t in BURST_ORDER if family == "esp32" or t not in ESP32_ONLY]


@dataclass
class BurstReport:
    frames: int = 0
    json_frames: int = 0
    other_frames: int = 0
    mh_frames: int = 0
    max_json_len: int = 0
    typs_seen: list[str] = field(default_factory=list)
    missing: list[str] = field(default_factory=list)
    missing_fields: dict[str, list[str]] = field(default_factory=dict)
    bad_frames: list[str] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)
    conffin: bool = False
    registers: dict[str, dict[str, Any]] = field(default_factory=dict)

    @property
    def ok(self) -> bool:
        return (
            self.conffin
            and not self.missing
            and not self.missing_fields
            and not self.bad_frames
        )

    def problems(self) -> str:
        parts: list[str] = []
        if not self.conffin:
            parts.append("no CONFFIN")
        if self.missing:
            parts.append("missing " + ",".join(self.missing))
        for typ, names in self.missing_fields.items():
            parts.append(f"{typ} lacks {','.join(names)}")
        parts.extend(self.bad_frames)
        return "; ".join(parts) or "ok"


def analyse_burst(frames: Sequence[bytes], family: str = "any") -> BurstReport:
    """Check the frames received after a hello against the expected burst."""
    rep = BurstReport()
    conffin_at: int | None = None
    typ_index: list[tuple[int, str]] = []
    for idx, frame in enumerate(frames):
        rep.frames += 1
        if frame[:1] != b"\x44":
            rep.other_frames += 1
            continue
        rep.json_frames += 1
        info = decode_json_frame(frame)
        rep.max_json_len = max(rep.max_json_len, info.json_len)
        if info.obj is None:
            rep.bad_frames.append(f"frame {idx}: invalid JSON ({info.error})")
            continue
        if info.json_len > BLE_JSON_PAYLOAD_MAX:
            rep.bad_frames.append(
                f"frame {idx} ({info.typ}): {info.json_len} B > {BLE_JSON_PAYLOAD_MAX}"
            )
        typ = info.typ
        if typ is None:
            rep.bad_frames.append(f"frame {idx}: no TYP")
            continue
        if typ == "MH":
            rep.mh_frames += 1
            continue
        if typ == "CONFFIN":
            rep.conffin = True
            conffin_at = idx
            continue
        if typ in rep.typs_seen:
            rep.warnings.append(f"duplicate register {typ}")
        else:
            rep.typs_seen.append(typ)
        typ_index.append((idx, typ))
        rep.registers[typ] = info.obj
        wanted = REGISTER_FIELDS.get(typ)
        if wanted is not None:
            lacking = [k for k in wanted if k not in info.obj]
            if lacking:
                rep.missing_fields[typ] = lacking
    rep.missing = [t for t in expected_typs(family) if t not in rep.typs_seen]
    if conffin_at is not None:
        late = [t for i, t in typ_index if i > conffin_at]
        if late:
            rep.warnings.append("register after CONFFIN: " + ",".join(late))
    order = [t for _, t in typ_index if t in BURST_ORDER]
    ranks = [BURST_ORDER.index(t) for t in order]
    if ranks != sorted(ranks):
        rep.warnings.append("registers out of config_cmds order: " + ",".join(order))
    return rep


def diff_registers(
    before: dict[str, dict[str, Any]], after: dict[str, dict[str, Any]]
) -> list[str]:
    """Differences between two register snapshots, live readings masked."""
    missing = object()

    def show(key: str, value: Any) -> str:
        if value is missing:
            return "<absent>"
        return "<redacted>" if key in SENSITIVE_KEYS else repr(value)

    out: list[str] = []
    for typ in sorted(after):
        b = before.get(typ)
        if b is None:
            continue
        for key in sorted(set(b) | set(after[typ])):
            if key in VOLATILE_JSON_KEYS:
                continue
            bv, av = b.get(key, missing), after[typ].get(key, missing)
            if bv != av:
                out.append(f"{typ}.{key}: {show(key, bv)} -> {show(key, av)}")
    return out


# --------------------------------------------------------------- transport


class Transport(Protocol):
    @property
    def is_connected(self) -> bool: ...

    @property
    def mtu(self) -> int: ...

    def set_notify(self, callback: Callable[[bytes], None]) -> None: ...

    async def write(self, frame: bytes, response: bool) -> None: ...


Connector = Callable[[], contextlib.AbstractAsyncContextManager[Transport]]


class NodeNotFound(Exception):
    pass


class BleakTransport:
    """The NUS characteristics of a connected bleak client."""

    def __init__(self, client: Any) -> None:
        self._client = client
        self._callback: Callable[[bytes], None] | None = None
        self._early: list[bytes] = []

    @property
    def is_connected(self) -> bool:
        return bool(self._client.is_connected)

    @property
    def mtu(self) -> int:
        try:
            return int(self._client.mtu_size)
        except Exception:  # noqa: BLE001 - backend without mtu_size
            return 0

    def on_data(self, _sender: Any, data: bytearray) -> None:
        if self._callback is None:
            self._early.append(bytes(data))
        else:
            self._callback(bytes(data))

    def set_notify(self, callback: Callable[[bytes], None]) -> None:
        self._callback = callback
        early, self._early = self._early, []
        for data in early:
            callback(data)

    async def write(self, frame: bytes, response: bool) -> None:
        await self._client.write_gatt_char(NUS_TX_CHAR, frame, response=response)


def bleak_connector(args: argparse.Namespace, emit: Emit) -> Connector:
    """Scan by name or address, connect, subscribe -- like ble_cycle.py."""

    @contextlib.asynccontextmanager
    async def connect() -> AsyncIterator[Transport]:
        from bleak import BleakClient, BleakScanner

        target = args.name or args.address
        emit(f"scanning for {target}")
        if args.address:
            # macOS caches advertised names; the CoreBluetooth UUID from
            # `ble_golden.py --scan` is the reliable handle there.
            dev = await BleakScanner.find_device_by_address(
                args.address, timeout=args.scan_seconds
            )
        else:
            suffix = "-" + args.name.upper()
            dev = await BleakScanner.find_device_by_filter(
                # exact callsign: "DK5EN-1" must not pick MC-xxxx-DK5EN-14
                lambda d, ad: (d.name or ad.local_name or "").upper().endswith(suffix),
                timeout=args.scan_seconds,
            )
        if dev is None:
            raise NodeNotFound(
                f"{target} not found in {args.scan_seconds:.0f} s "
                "(the node holds one connection: disconnect the phone)"
            )
        async with BleakClient(dev, timeout=20) as client:
            tr = BleakTransport(client)
            await client.start_notify(NUS_RX_CHAR, tr.on_data)
            yield tr

    return connect


# --------------------------------------------------------------- session


class Session:
    """One connection as the app uses it: notifications in, guarded writes out."""

    def __init__(
        self,
        transport: Transport,
        emit: Emit,
        allow_settings: frozenset[int] = frozenset(),
        allow_save: bool = False,
    ) -> None:
        self.tr = transport
        self.emit = emit
        self.allow_settings = allow_settings
        self.allow_save = allow_save
        self.t0 = time.monotonic()
        self.rx: list[tuple[float, bytes]] = []
        self.conffin_count = 0
        self.hello_mark = 0
        self.hello_conffin = 0
        transport.set_notify(self._on_rx)

    @property
    def connected(self) -> bool:
        return self.tr.is_connected

    def _on_rx(self, data: bytes) -> None:
        self.rx.append((time.monotonic() - self.t0, data))
        if register_of(data) == "CONFFIN":
            self.conffin_count += 1
        self.emit(f"RX  {describe_rx(data)}")

    def mark(self) -> int:
        return len(self.rx)

    def frames_since(self, mark: int) -> list[bytes]:
        return [data for _, data in self.rx[mark:]]

    def typs_since(self, mark: int) -> list[str]:
        out: list[str] = []
        for frame in self.frames_since(mark):
            typ = register_of(frame)
            if typ is not None:
                out.append(typ)
        return out

    async def send(self, frame: bytes, label: str, response: bool = True) -> None:
        guard_frame(frame, self.allow_settings, self.allow_save)
        self.emit(f"TX  {label} len={len(frame)} {frame_repr(frame)}")
        await self.tr.write(frame, response)

    async def wait_for(self, pred: Callable[[], bool], timeout: float) -> bool:
        end = time.monotonic() + timeout
        while True:
            if pred():
                return True
            if time.monotonic() >= end:
                return False
            await asyncio.sleep(POLL)

    async def wait_quiet(self, quiet: float, maximum: float) -> float:
        """Until no notification arrived for `quiet` s, at most `maximum` s."""
        start = time.monotonic()
        last_count, last_change = -1, start
        while time.monotonic() - start < maximum:
            if len(self.rx) != last_count:
                last_count, last_change = len(self.rx), time.monotonic()
            elif time.monotonic() - last_change >= quiet:
                break
            await asyncio.sleep(POLL)
        return time.monotonic() - start

    async def hello(self, pin: int | None) -> None:
        kind = "open" if pin is None else "pin"
        # Taken BEFORE the write: the burst can start before the write's ATT
        # acknowledgement returns, and a mark taken afterwards would lose it.
        self.hello_mark, self.hello_conffin = self.mark(), self.conffin_count
        await self.send(build_hello(pin), f"hello({kind})")

    async def collect_burst(self, timeout: float, family: str) -> BurstReport:
        """Everything after the hello, until CONFFIN, a drop, or `timeout`."""
        start, base = self.hello_mark, self.hello_conffin
        await self.wait_for(
            lambda: self.conffin_count > base or not self.connected, timeout
        )
        await asyncio.sleep(0.2)  # a late frame behind CONFFIN would be a finding
        return analyse_burst(self.frames_since(start), family)

    async def probe(self, timeout: float) -> float | None:
        """`--info`, then wait for the `I` register. Seconds, or None."""
        mark, t = self.mark(), time.monotonic()
        try:
            await self.send(build_command("--info"), "probe --info")
        except RefusedFrame:
            raise
        except Exception as exc:  # noqa: BLE001 - a dead link raises what the backend raises
            self.emit(f"probe write failed: {type(exc).__name__}: {exc}")
            return None
        got = await self.wait_for(lambda: "I" in self.typs_since(mark), timeout)
        return time.monotonic() - t if got else None

    async def read_command(
        self, cmd: str, timeout: float
    ) -> tuple[dict[str, dict[str, Any]], int]:
        """One read-only command: (registers by TYP, frames received)."""
        mark = self.mark()
        want = set(COMMAND_REPLIES[cmd])
        await self.send(build_command(cmd), cmd)
        if want:
            await self.wait_for(
                lambda: want <= set(self.typs_since(mark)) or not self.connected,
                timeout,
            )
        regs: dict[str, dict[str, Any]] = {}
        frames = self.frames_since(mark)
        for frame in frames:
            info = decode_json_frame(frame) if frame[:1] == b"\x44" else None
            if info and info.obj and info.typ in want:
                regs[info.typ] = info.obj
        return regs, len(frames)

    async def read_msgid(self, timeout: float) -> int | None:
        mark = self.mark()
        await self.send(build_command("--msgid"), "--msgid")
        found: list[int] = []

        def seen() -> bool:
            for frame in self.frames_since(mark):
                value = msgid_of(frame)
                if value is not None:
                    found.append(value)
                    return True
            return False

        await self.wait_for(seen, timeout)
        return found[0] if found else None


@contextlib.asynccontextmanager
async def open_session(
    connector: Connector,
    args: argparse.Namespace,
    emit: Emit,
    allow_settings: frozenset[int] = frozenset(),
    allow_save: bool = False,
) -> AsyncIterator[Session]:
    async with connector() as tr:
        session = Session(tr, emit, allow_settings, allow_save)
        emit("connected" + (f" mtu={tr.mtu}" if args.mtu else ""))
        try:
            await asyncio.sleep(args.connect_wait)
            yield session
        finally:
            emit(f"closing (link {'up' if session.connected else 'already down'})")
    emit("disconnected")


class Reconnecting:
    """A session that comes back after the node kills or drops the link."""

    def __init__(
        self,
        connector: Connector,
        args: argparse.Namespace,
        emit: Emit,
        family: str,
    ) -> None:
        self.connector, self.args, self.emit, self.family = (
            connector,
            args,
            emit,
            family,
        )
        self.session: Session | None = None
        self._cm: contextlib.AbstractAsyncContextManager[Session] | None = None

    async def ensure(self) -> tuple[Session, bool]:
        """(session, freshly connected)."""
        if self.session is not None and self.session.connected:
            return self.session, False
        await self.close()
        self._cm = open_session(self.connector, self.args, self.emit)
        self.session = await self._cm.__aenter__()
        await self.session.hello(self.args.pin_value)
        rep = await self.session.collect_burst(self.args.timeout, self.family)
        self.emit(f"burst after reconnect: {rep.problems()}")
        return self.session, True

    async def close(self) -> None:
        cm, self._cm, self.session = self._cm, None, None
        if cm is not None:
            with contextlib.suppress(Exception):
                await cm.__aexit__(None, None, None)


# --------------------------------------------------------------- subcommands


def _is_bluetooth_unavailable(exc: BaseException) -> bool:
    return type(exc).__name__ == "BleakBluetoothNotAvailableError"


async def cmd_burst(
    connector: Connector, args: argparse.Namespace, emit: Emit, family: str
) -> Verdict:
    expected = expected_typs(family)
    total: Counter[str] = Counter()
    failed = 0
    for i in range(1, args.cycles + 1):
        emit(f"cycle {i}/{args.cycles} start")
        rep: BurstReport | None = None
        error = ""
        try:
            async with open_session(connector, args, emit) as s:
                await s.hello(args.pin_value)
                if args.timesync:
                    await s.send(build_timesync(int(time.time())), "timesync")
                rep = await s.collect_burst(args.timeout, family)
        except Exception as exc:  # bleak raises many concrete types
            if _is_bluetooth_unavailable(exc):
                raise
            error = f"{type(exc).__name__}: {exc}"
        if rep is None:
            failed += 1
            total.update(expected)
            emit(f"cycle {i} FAIL connect/transfer error: {error}")
        else:
            total.update(rep.missing)
            for warning in rep.warnings:
                emit(f"cycle {i} WARN {warning}")
            emit(
                f"cycle {i} {'PASS' if rep.ok else 'FAIL'} frames={rep.frames} "
                f"json={rep.json_frames} mh={rep.mh_frames} other={rep.other_frames} "
                f"max_json={rep.max_json_len} seen={','.join(rep.typs_seen)} "
                f"missing={','.join(rep.missing) or '-'} :: {rep.problems()}"
            )
            if not rep.ok:
                failed += 1
        if i < args.cycles:
            await asyncio.sleep(args.gap)
    missing = ", ".join(f"{t}x{n}" for t, n in sorted(total.items())) or "none"
    return Verdict(
        failed == 0,
        f"burst: {args.cycles - failed}/{args.cycles} cycles complete, "
        f"missing registers total: {missing}",
    )


def wrong_pin(pin: int) -> int:
    return (pin + 1) % 1_000_000


async def _expect_rejected(
    connector: Connector, args: argparse.Namespace, emit: Emit, pin: int | None
) -> tuple[bool, str]:
    """Send a hello the node must refuse: no burst, and the link dropped."""
    async with open_session(connector, args, emit) as s:
        mark = s.mark()
        await s.hello(pin)
        await s.wait_for(
            lambda: bool(s.typs_since(mark)) or not s.connected, args.reject_wait
        )
        await asyncio.sleep(0.3)
        burst = bool(s.typs_since(mark))
        dropped = not s.connected
        emit(f"result: burst={'yes' if burst else 'no'} dropped={dropped}")
        if burst:
            return False, "got a burst"
        if not dropped:
            return False, "no burst but the link stayed up"
        return True, "no burst, link dropped"


async def _expect_burst(
    connector: Connector,
    args: argparse.Namespace,
    emit: Emit,
    family: str,
    pin: int | None,
    want_pin: bool,
) -> tuple[bool, str]:
    async with open_session(connector, args, emit) as s:
        await s.hello(pin)
        rep = await s.collect_burst(args.timeout, family)
        for warning in rep.warnings:
            emit(f"WARN {warning}")
        if not rep.ok:
            return False, rep.problems()
        bpin = rep.registers.get("I", {}).get("BPIN")
        # BPIN is the node's PIN in plain: only its presence is reported.
        emit(f"I.BPIN is {'set' if bpin else 'zero'} (value not shown)")
        if isinstance(bpin, int) and bool(bpin) != want_pin:
            return False, f"burst ok but BPIN is {'set' if bpin else 'zero'}"
        return True, "burst complete"


async def cmd_pin(
    connector: Connector, args: argparse.Namespace, emit: Emit, family: str
) -> Verdict:
    results: list[tuple[str, bool, str]] = []

    async def scenario(name: str, coro: Any) -> None:
        emit(f"scenario {name}")
        try:
            ok, detail = await coro
        except Exception as exc:
            if _is_bluetooth_unavailable(exc):
                raise
            ok, detail = False, f"{type(exc).__name__}: {exc}"
        emit(f"scenario {name} {'PASS' if ok else 'FAIL'}: {detail}")
        results.append((name, ok, detail))

    if args.expect_pin == "none":
        await scenario(
            "open hello, node without PIN",
            _expect_burst(connector, args, emit, family, None, False),
        )
    else:
        pin = args.pin_value
        await scenario(
            "correct hash",
            _expect_burst(connector, args, emit, family, pin, True),
        )
        await asyncio.sleep(args.gap)
        await scenario(
            "wrong hash",
            _expect_rejected(connector, args, emit, wrong_pin(pin)),
        )
        await asyncio.sleep(args.gap)
        await scenario(
            "open hello, node with PIN",
            _expect_rejected(connector, args, emit, None),
        )
    bad = [n for n, ok, _ in results if not ok]
    return Verdict(
        not bad,
        f"pin (expect {args.expect_pin}): {len(results) - len(bad)}/{len(results)} "
        "scenarios" + (f", failed: {'; '.join(bad)}" if bad else ""),
    )


async def _flood_texts(
    connector: Connector, args: argparse.Namespace, emit: Emit, family: str
) -> Verdict:
    dest = check_destination(args.dm or args.group)
    own = tuple(c.strip() for c in (args.own_call or args.name or "").split(",") if c)
    if not own:
        emit("WARN own callsign unknown (--own-call/--name): broadcast check inactive")
    runid = time.strftime("%H%M%S")
    async with open_session(connector, args, emit) as s:
        await s.hello(args.pin_value)
        rep = await s.collect_burst(args.timeout, family)
        if not rep.ok:
            emit(f"WARN burst incomplete before the flood: {rep.problems()}")
        emit(f"NOTE every text below is transmitted on air to {{{dest}}}")
        start, sent, error = s.mark(), 0, ""
        for i in range(1, args.count + 1):
            frame = build_group_text(dest, f"ble-stress {runid} {i}/{args.count}")
            try:
                await s.send(
                    frame, f"text {i}/{args.count}", response=not args.no_response
                )
            except RefusedFrame:
                raise
            except Exception as exc:  # noqa: BLE001 - dead link, backend-specific type
                error = f"write {i} failed: {type(exc).__name__}: {exc}"
                emit(error)
                break
            sent += 1
            if args.interval > 0:
                await asyncio.sleep(args.interval)
        await s.wait_quiet(args.quiet, args.drain)
        alive = s.connected
        latency = await s.probe(args.probe_timeout) if alive else None
        frames = s.frames_since(start)
    acks = sum(1 for f in frames if len(f) > 1 and f[0] == 0x40 and f[1] == 0x41)
    texts = sum(1 for f in frames if len(f) > 1 and f[0] == 0x40 and f[1] == 0x3A)
    broadcasts = verify_no_broadcast([(0.0, f) for f in frames], own)
    for line in broadcasts:
        emit(f"BROADCAST {line}")
    emit(
        f"flood done: sent={sent}/{args.count} acks={acks} text_frames_rx={texts} "
        f"link={'up' if alive else 'DOWN'} probe="
        f"{f'{latency:.2f}s' if latency is not None else 'NO REPLY'}"
    )
    ok = sent == args.count and alive and latency is not None and not broadcasts
    why = []
    if sent != args.count:
        why.append(f"only {sent}/{args.count} writes accepted")
    if not alive:
        why.append("link dropped")
    elif latency is None:
        why.append("link hung: no reply to --info afterwards")
    if broadcasts:
        why.append("BROADCAST transmitted")
    return Verdict(
        ok,
        f"flood text: {sent}/{args.count} sent to {{{dest}}}, link alive, "
        "answers --info, no broadcast"
        if ok
        else "flood text: " + "; ".join(why),
    )


async def _flood_commands(
    connector: Connector, args: argparse.Namespace, emit: Emit, family: str
) -> Verdict:
    cmds = tuple(args.flood_cmds) or DEFAULT_FLOOD_COMMANDS
    for cmd in cmds:
        build_command(cmd)  # allowlist check up front
    expected: Counter[str] = Counter()
    for cmd in cmds:
        for typ in COMMAND_REPLIES[cmd]:
            expected[typ] += args.rounds
    async with open_session(connector, args, emit) as s:
        await s.hello(args.pin_value)
        rep = await s.collect_burst(args.timeout, family)
        if not rep.ok:
            emit(f"WARN burst incomplete before the flood: {rep.problems()}")
        start, sent, error = s.mark(), 0, ""
        for rnd in range(1, args.rounds + 1):
            for cmd in cmds:
                try:
                    await s.send(
                        build_command(cmd),
                        f"round {rnd}/{args.rounds} {cmd}",
                        response=not args.no_response,
                    )
                except RefusedFrame:
                    raise
                except Exception as exc:  # noqa: BLE001 - dead link, backend-specific
                    error = f"write failed: {type(exc).__name__}: {exc}"
                    emit(error)
                    break
                sent += 1
                if args.interval > 0:
                    await asyncio.sleep(args.interval)
            if error:
                break
        # the command ring drains one frame per 300 ms
        drain = max(args.drain, DRAIN_PER_FRAME * sum(expected.values()) + DRAIN_BASE)
        await s.wait_quiet(max(args.quiet, FLOOD_QUIET_MIN), drain)
        alive = s.connected
        got = Counter(t for t in s.typs_since(start) if t in expected)
    rows = []
    short = 0
    for typ in sorted(expected):
        miss = max(0, expected[typ] - got[typ])
        short += miss
        rows.append(f"{typ} {got[typ]}/{expected[typ]}")
        if miss:
            emit(f"MISSING {typ}: {miss} of {expected[typ]}")
    emit(f"replies received/expected: {', '.join(rows)}")
    total_exp = sum(expected.values())
    ok = alive and not short and not error
    why = []
    if short:
        why.append(f"{short} of {total_exp} replies missing")
    if not alive:
        why.append("link dropped")
    if error:
        why.append(error)
    return Verdict(
        ok,
        f"flood commands: {total_exp - short}/{total_exp} replies for {sent} "
        f"commands ({', '.join(cmds)}), link alive"
        if ok
        else "flood commands: " + "; ".join(why),
    )


async def cmd_flood(
    connector: Connector, args: argparse.Namespace, emit: Emit, family: str
) -> Verdict:
    if args.commands:
        return await _flood_commands(connector, args, emit, family)
    return await _flood_texts(connector, args, emit, family)


def symbol_frame_from_register(sa: dict[str, Any]) -> bytes:
    """The 0x95 frame that writes back exactly what `SA` reported."""
    symid, symcd = sa.get("SYMID"), sa.get("SYMCD")
    if not (isinstance(symid, str) and isinstance(symcd, str)):
        raise TypeError("SA lacks SYMID/SYMCD")
    if len(symid) != 1 or len(symcd) != 1 or ord(symcd) > 0x7F:
        raise ValueError(f"SA symbol {symid!r}/{symcd!r} is not one ASCII char each")
    if symid not in ("/", "\\"):
        # phone_commands.cpp:531 ignores anything else: nothing to write back
        raise ValueError(f"SA symbol table {symid!r} is not writable over 0x95")
    return build_symbol(symid, symcd)


ROUNDTRIP_READS = ("--aprsset", "--nodeset", "--seset", "--wifiset", "--info")


async def cmd_roundtrip(
    connector: Connector, args: argparse.Namespace, emit: Emit, family: str
) -> Verdict:
    async with open_session(
        connector,
        args,
        emit,
        allow_settings=frozenset({OP_SYMBOL}),
        allow_save=args.allow_save,
    ) as s:
        await s.hello(args.pin_value)
        rep = await s.collect_burst(args.timeout, family)
        if not rep.ok:
            emit(f"WARN burst incomplete: {rep.problems()}")
        sa = rep.registers.get("SA")
        if sa is None:
            return Verdict(False, "settings-roundtrip: no SA register in the burst")
        try:
            frame = symbol_frame_from_register(sa)
        except (ValueError, TypeError) as exc:
            return Verdict(False, f"settings-roundtrip: nothing to write back: {exc}")
        before = rep.registers
        msgid_before = await s.read_msgid(args.probe_timeout)
        emit(f"msgid before={msgid_before if msgid_before is not None else 'no reply'}")
        emit("MARK before-write (operator: compare --info over serial now)")
        await asyncio.sleep(args.pause)
        await s.send(frame, f"0x95 symbol {sa['SYMID']!r}/{sa['SYMCD']!r} (unchanged)")
        emit(f"WROTE 0x95 SYMID={sa['SYMID']!r} SYMCD={sa['SYMCD']!r} (identical)")
        await asyncio.sleep(args.settle)
        emit("MARK after-write")
        await asyncio.sleep(args.pause)
        after: dict[str, dict[str, Any]] = {}
        for cmd in ROUNDTRIP_READS:
            regs, _ = await s.read_command(cmd, args.probe_timeout)
            after.update(regs)
        msgid_after = await s.read_msgid(args.probe_timeout)
        emit(f"msgid after={msgid_after if msgid_after is not None else 'no reply'}")
        if (
            msgid_before is not None
            and msgid_after is not None
            and msgid_after != msgid_before
        ):
            emit(
                "NOTE msgid moved: the node transmitted meanwhile "
                "(beacon or relay), not necessarily this write"
            )
        diffs = diff_registers(before, after)
        for line in diffs:
            emit(f"DIFF {line}")
        no_sa = "SA" not in after
        if no_sa:
            emit("FAIL SA not re-read after the write")
        saved = ""
        if args.allow_save:
            emit("WARNING --allow-save: sending 0xF0, the node saves and RESETS")
            await s.send(build_save(), "0xF0 save+reset")
            await s.wait_for(lambda: not s.connected, 15.0)
            saved = f", 0xF0 sent, link {'dropped' if not s.connected else 'still up'}"
    ok = not diffs and not no_sa
    return Verdict(
        ok,
        (
            f"settings-roundtrip: 0x95 {sa['SYMID']}{sa['SYMCD']} written back, "
            f"{sum(len(v) for v in after.values())} fields re-read, no difference"
            if ok
            else f"settings-roundtrip: {len(diffs)} field(s) differ after the write"
        )
        + saved,
    )


async def cmd_malformed(
    connector: Connector, args: argparse.Namespace, emit: Emit, family: str
) -> Verdict:
    frames = [f for f in malformed_frames(args.group) if f.name not in args.skip]
    rc = Reconnecting(connector, args, emit, family)
    results: list[tuple[str, str, str]] = []
    try:
        for mf in frames:
            emit(f"frame {mf.name}: {mf.note}")
            try:
                s, fresh = await rc.ensure()
                if fresh and await s.probe(args.probe_timeout) is None:
                    results.append((mf.name, "NO-BASELINE", "link mute before frame"))
                    emit(f"frame {mf.name} NO-BASELINE")
                    continue
                mark = s.mark()
                try:
                    await s.send(mf.frame, mf.name)
                except RefusedFrame:
                    raise
                except Exception as exc:  # noqa: BLE001 - dead link, backend-specific
                    verdict, detail = "KILLED", f"write failed: {type(exc).__name__}"
                else:
                    await asyncio.sleep(args.settle)
                    await s.wait_quiet(args.quiet, args.drain)
                    replies = sorted(set(s.typs_since(mark)))
                    detail = "replies " + (",".join(replies) or "none")
                    if not s.connected:
                        verdict = "KILLED"
                    elif await s.probe(args.probe_timeout) is None:
                        verdict = "KILLED" if not s.connected else "HUNG"
                    else:
                        verdict = "OK"
            except RefusedFrame:
                raise
            except Exception as exc:
                if _is_bluetooth_unavailable(exc):
                    raise
                verdict, detail = "ERROR", f"{type(exc).__name__}: {exc}"
            emit(f"frame {mf.name} {verdict} ({detail})")
            results.append((mf.name, verdict, detail))
            if verdict != "OK":
                # a hung link stays up: start the next frame on a fresh one
                await rc.close()
    finally:
        await rc.close()
    bad = [(n, v) for n, v, _ in results if v != "OK"]
    return Verdict(
        not bad and bool(results),
        f"malformed: {len(results) - len(bad)}/{len(results)} frames survived"
        + ("; " + ", ".join(f"{n} {v}" for n, v in bad) if bad else ""),
    )


COMMANDS: dict[
    str,
    Callable[[Connector, argparse.Namespace, Emit, str], Any],
] = {
    "burst": cmd_burst,
    "pin": cmd_pin,
    "flood": cmd_flood,
    "settings-roundtrip": cmd_roundtrip,
    "malformed": cmd_malformed,
}


# --------------------------------------------------------------- self-test


def _self_test() -> int:
    failures: list[str] = []
    checks = 0

    def check(label: str, cond: bool) -> None:
        nonlocal checks
        checks += 1
        if not cond:
            failures.append(label)
            print(f"FAIL: {label}")

    def refused(label: str, frame: bytes, **kw: Any) -> None:
        try:
            guard_frame(frame, **kw)
        except RefusedFrame:
            check(label, True)
        else:
            check(label + " (was accepted)", False)

    # builders
    check("open hello", build_hello(None) == bytes.fromhex("04102030"))
    check("pin hello shape", len(build_hello(123456)) == 36)
    t = build_group_text("9", "hi")
    check("group text is one colon", t == bytes([8, 0xA0]) + b":{9}hi")
    check("group 9999", build_group_text("9999", "x")[2:] == b":{9999}x")
    dm = build_group_text("dk5en-14", "x")
    check("DM upper-cased", dm[2:] == b":{DK5EN-14}x")
    check("command frame", build_command("--info") == bytes([8, 0xA0]) + b"--info")
    check("symbol frame", build_symbol("/", "#") == bytes([4, 0x95, 0x2F, 0x23]))
    check("save frame", build_save() == bytes([2, 0xF0]))

    # destination guard
    for bad in ("*", "", "all", "OE1XAR-62", "DK5EN", "DK5EN-1234", "9 ", "99", "{9}"):
        try:
            check_destination(bad)
        except RefusedFrame:
            check(f"destination {bad!r} refused", True)
        else:
            if bad.strip() == "9":  # "9 " strips to group 9: allowed by design
                check("destination '9 ' normalised", True)
            else:
                check(f"destination {bad!r} refused", False)
    for good in ("9", "9999", "DK5EN-1", "DK5EN-90", "dk5en-14", "DK5EN-100"):
        try:
            check_destination(good)
            check(f"destination {good!r} allowed", True)
        except RefusedFrame:
            check(f"destination {good!r} allowed", False)
    try:
        build_group_text("9", "x" * TEXT_MAX_CHARS)
    except RefusedFrame:
        check("text over 160 chars refused", True)
    else:
        check("text over 160 chars refused", False)

    # frame guard
    refused("bare text (broadcast)", build_text("hello"))
    refused("two-colon form", build_text("::{9}hi"))
    refused("no destination", build_text(":hi"))
    refused("broadcast destination", build_text(":{*}hi"))
    refused("padded destination", build_text(":{ 9 }hi"))
    refused("lower-case DM destination", build_text(":{dk5en-14}hi"))
    refused("foreign callsign", build_text(":{OE1XAR-62}hi"))
    refused("write command", build_text("--mesh off"))
    refused("callsign opcode", bytes([6, 0x50, 3, 0x41, 0x42, 0x43]))
    refused("wifi opcode", bytes([4, 0x55, 0, 0]))
    refused("latitude opcode", bytes([7, 0x70, 0, 0, 0, 0, 0x0B]))
    refused("altitude opcode", bytes([7, 0x90, 0, 0, 0, 0, 0x0B]))
    refused("0x95 outside roundtrip", build_symbol("/", "#"))
    refused("0xF0 without --allow-save", build_save())
    refused("unknown high opcode", bytes([4, 0xEE, 0, 0]))
    refused("short frame", b"\x01")
    for ok_label, frame, kw in (
        ("hello", build_hello(None), {}),
        ("group text", t, {}),
        ("read-only command", build_command("--seset"), {}),
        ("0x95 in roundtrip", build_symbol("/", "#"), {"allow_settings": {0x95}}),
        ("0xF0 with --allow-save", build_save(), {"allow_save": True}),
        ("unknown low opcode", bytes([4, 0x33, 0, 0]), {}),
    ):
        try:
            guard_frame(frame, **kw)  # type: ignore[arg-type]
            check(f"guard accepts {ok_label}", True)
        except RefusedFrame:
            check(f"guard accepts {ok_label}", False)
    try:
        build_command("--mesh off")
    except RefusedFrame:
        check("build_command allowlist", True)
    else:
        check("build_command allowlist", False)

    # every malformed frame passes the guard, and the text among them is safe
    mfs = malformed_frames()
    check("six malformed frames", len(mfs) == 6)
    for mf in mfs:
        try:
            guard_frame(mf.frame)
            check(f"malformed {mf.name} passes the guard", True)
        except RefusedFrame:
            check(f"malformed {mf.name} passes the guard", False)
    by = {m.name: m.frame for m in mfs}
    check(
        "len-too-big declares more than it has",
        by["len-too-big"][0] > len(by["len-too-big"]),
    )
    check("len-zero", by["len-zero"][0] == 0)
    check("len-one", by["len-one"][0] == 1)
    check("truncated hello is 2 bytes", by["truncated-hello"] == bytes([4, 0x10]))
    over = by["oversized-text"]
    check("oversized text > 160 chars", len(over) - 2 > TEXT_MAX_CHARS)
    check("oversized text is group 9, one colon", over[2:7] == b":{9}x")
    check("oversized length byte is total length", over[0] == len(over))

    # pin hello redaction
    check("hello redacted in log", "<pin-hash-32>" in frame_repr(build_hello(1)))
    check(
        "hash absent from log",
        build_hello(1)[4:].hex() not in frame_repr(build_hello(1)),
    )
    check("wrong pin differs", wrong_pin(0) != 0 and wrong_pin(999999) == 0)
    sanitized = redact_obj({"BPIN": 123456, "WSPWD": "s3cret", "CALL": "X"})
    check(
        "BPIN/WSPWD redacted",
        sanitized == {"BPIN": "<redacted>", "WSPWD": "<redacted>", "CALL": "X"},
    )

    # burst analysis on a synthetic, complete burst
    def register(typ: str) -> bytes:
        return json_frame({"TYP": typ, **{k: 0 for k in REGISTER_FIELDS[typ]}})

    burst = [register(t) for t in expected_typs("esp32")]
    burst.insert(-3, json_frame({"TYP": "MH", "CALL": "DK5EN-1"}))
    burst.append(register("CONFFIN"))
    rep = analyse_burst(burst, "esp32")
    check("complete burst is ok", rep.ok and rep.mh_frames == 1)
    no_an = [f for f in burst if register_of(f) != "AN"]
    check("missing AN caught on esp32", analyse_burst(no_an, "esp32").missing == ["AN"])
    check("AN not required on nrf52", analyse_burst(no_an, "nrf52").ok)
    check("AN not required for family any", analyse_burst(no_an, "any").ok)
    no_sn = [f for f in burst if register_of(f) != "SN"]
    check("missing SN caught", analyse_burst(no_sn, "any").missing == ["SN"])
    check("no CONFFIN caught", not analyse_burst(burst[:-1], "any").ok)
    cut = json.loads(burst[expected_typs("esp32").index("SN")][1:].rstrip(b"\0"))
    del cut["GWS"]
    trimmed = list(burst)
    trimmed[expected_typs("esp32").index("SN")] = json_frame(cut)
    rep = analyse_burst(trimmed, "esp32")
    check("dropped trailing SN field caught", rep.missing_fields == {"SN": ["GWS"]})
    big = list(burst)
    big[0] = json_frame({"TYP": "I", **{k: "x" * 30 for k in REGISTER_FIELDS["I"]}})
    check("oversized frame caught", bool(analyse_burst(big, "esp32").bad_frames))
    bad_json = list(burst)
    bad_json[1] = b"\x44{not json\x00\x00"
    check("invalid JSON caught", bool(analyse_burst(bad_json, "esp32").bad_frames))
    dup = burst[:-1] + [burst[0], burst[-1]]
    check(
        "duplicate is a warning, not a failure",
        analyse_burst(dup, "esp32").ok and bool(analyse_burst(dup, "esp32").warnings),
    )
    check("SN lists all 18 SN fields", len(REGISTER_FIELDS["SN"]) == 18)
    check("SN1 fields", REGISTER_FIELDS["SN1"] == ("VIA", "VIACALL", "WSPWD", "ASYM"))

    # helpers
    check("msgid parse", msgid_of(b"\x40:\x01--msgid 4711\n\x00") == 4711)
    check("msgid ignores others", msgid_of(b"\x44{}") is None)
    check("register_of", register_of(json_frame({"TYP": "SN1"})) == "SN1")
    diffs = diff_registers(
        {"SN": {"GW": 1, "BATV": 4.1, "WSPWD": "a"}},
        {"SN": {"GW": 1, "BATV": 3.9, "WSPWD": "b"}},
    )
    check(
        "volatile masked, sensitive redacted",
        diffs == ["SN.WSPWD: <redacted> -> <redacted>"],
    )
    check(
        "symbol from register",
        symbol_frame_from_register({"SYMID": "/", "SYMCD": "#"})
        == build_symbol("/", "#"),
    )
    try:
        symbol_frame_from_register({"SYMID": "X", "SYMCD": "#"})
    except ValueError:
        check("unwritable symbol table refused", True)
    else:
        check("unwritable symbol table refused", False)
    check(
        "timesync frame",
        build_timesync(1)[:2] == bytes([6, 0x20]) and struct.calcsize("<i") == 4,
    )
    check(
        "every command has an entry",
        set(DEFAULT_FLOOD_COMMANDS) <= set(COMMAND_REPLIES),
    )

    ok = not failures
    summary = f"self-test: {checks} checks" + (
        "" if ok else f", {len(failures)} failed"
    )
    say(("PASS " if ok else "FAIL ") + summary)
    return 0 if ok else 1


# --------------------------------------------------------------- cli


def build_parser() -> argparse.ArgumentParser:
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument(
        "--name", default="", help="callsign, exact suffix of MC-<id>-<call>"
    )
    common.add_argument(
        "--address",
        default="",
        help="peripheral address/UUID from ble_golden.py --scan",
    )
    common.add_argument(
        "--own-call",
        default=None,
        help="callsign(s) the node originates as (default --name)",
    )
    common.add_argument(
        "--family",
        choices=("esp32", "nrf52", "any"),
        default=None,
        help="AN is expected on esp32 only (default: from fleet.json via --node, else any)",
    )
    common.add_argument("--pin", type=int, default=None, help="six-digit app PIN")
    common.add_argument(
        "--pin-file", type=Path, default=None, help="read the PIN from a file"
    )
    common.add_argument("--scan-seconds", type=float, default=15.0)
    common.add_argument(
        "--connect-wait",
        type=float,
        default=1.5,
        help="seconds after subscribe before the first write",
    )
    common.add_argument(
        "--timeout", type=float, default=90.0, help="seconds to wait for CONFFIN"
    )
    common.add_argument(
        "--settle", type=float, default=1.0, help="seconds to let a write take effect"
    )
    common.add_argument(
        "--quiet", type=float, default=1.5, help="seconds of silence that end a drain"
    )
    common.add_argument(
        "--drain", type=float, default=15.0, help="longest wait for replies to drain"
    )
    common.add_argument(
        "--probe-timeout",
        type=float,
        default=8.0,
        help="wait for the `I` reply to an --info probe",
    )
    common.add_argument(
        "--gap", type=float, default=5.0, help="seconds between connections"
    )
    common.add_argument(
        "--mtu", action="store_true", help="print the negotiated MTU after connect"
    )
    common.add_argument(
        "--timesync",
        action="store_true",
        help="also send the app's 0x20 timesync (current UTC); off by default because it sets the node clock",
    )
    add_guard_args(common)

    ap = argparse.ArgumentParser(
        description=__doc__.split("\n")[0],
        epilog="Each subcommand ends with a PASS/FAIL line; exit 0 = PASS, 1 = FAIL.",
    )
    ap.add_argument(
        "--self-test", action="store_true", help="offline: builders, guards, analysis"
    )
    sub = ap.add_subparsers(dest="cmd")

    p = sub.add_parser(
        "burst", parents=[common], help="hello and check the config burst"
    )
    p.add_argument("--cycles", type=int, default=1)

    p = sub.add_parser("pin", parents=[common], help="PIN handling")
    p.add_argument(
        "--expect-pin",
        choices=("none", "set"),
        required=True,
        help="the node's state, set by the operator over serial",
    )
    p.add_argument(
        "--reject-wait",
        type=float,
        default=8.0,
        help="seconds a refused hello is watched",
    )

    p = sub.add_parser(
        "flood",
        parents=[common],
        help="texts to a group, or read-only commands in a tight loop",
    )
    p.add_argument(
        "--count",
        type=int,
        default=10,
        help=f"texts to send (max {FLOOD_MAX}); each one goes on air",
    )
    p.add_argument("--group", default="9", help="9 or 9999")
    p.add_argument(
        "--dm",
        default=None,
        help="direct message to a DK5EN-<ssid> node instead of a group",
    )
    p.add_argument("--interval", type=float, default=0.0, help="seconds between writes")
    p.add_argument(
        "--no-response", action="store_true", help="write without response (tighter)"
    )
    p.add_argument(
        "--commands",
        action="store_true",
        help="flood read-only commands instead of texts",
    )
    p.add_argument(
        "--cmd",
        dest="flood_cmds",
        action="append",
        default=[],
        help="read-only command for --commands, without the dashes, repeatable "
        f"(default {' '.join(c[2:] for c in DEFAULT_FLOOD_COMMANDS)})",
    )
    p.add_argument(
        "--rounds",
        type=int,
        default=20,
        help="--commands: passes over the command list",
    )

    p = sub.add_parser(
        "settings-roundtrip",
        parents=[common],
        help="write identical values back via 0x95",
    )
    p.add_argument(
        "--allow-save",
        action="store_true",
        help="afterwards send 0xF0: save and RESET the node",
    )
    p.add_argument(
        "--pause",
        type=float,
        default=0.0,
        help="seconds to pause at each MARK line for the operator",
    )

    p = sub.add_parser(
        "malformed",
        parents=[common],
        help="malformed frames, then check the link answers",
    )
    p.add_argument(
        "--group", default="9", help="group of the oversized text (9 or 9999)"
    )
    p.add_argument(
        "--skip",
        action="append",
        default=[],
        help="frame name to leave out, repeatable",
    )
    return ap


def prepare(ap: argparse.ArgumentParser, args: argparse.Namespace) -> None:
    """Validate arguments and resolve pin / name / family."""
    if args.pin_file is not None:
        try:
            args.pin = int(args.pin_file.read_text().strip())
        except (OSError, ValueError):
            ap.error("--pin-file: unreadable or not a number")
    if args.pin is not None and not 0 <= args.pin <= 999999:
        ap.error("PIN must be 0..999999")
    args.pin_value = args.pin
    if args.cmd == "pin" and args.expect_pin == "set" and args.pin is None:
        ap.error("pin --expect-pin set needs --pin or --pin-file")
    if args.cmd == "flood":
        if not 1 <= args.count <= FLOOD_MAX:
            ap.error(f"--count must be 1..{FLOOD_MAX}")
        try:
            check_destination(args.dm or args.group)
        except RefusedFrame as exc:
            ap.error(str(exc))
        # `--cmd info` (argparse would take `--cmd --info` for two options)
        args.flood_cmds = [
            c if c.startswith("--") else "--" + c for c in args.flood_cmds
        ]
        for cmd in args.flood_cmds:
            if cmd not in COMMAND_REPLIES:
                ap.error(f"--cmd {cmd!r} is not on the read-only allowlist")
    if args.cmd == "malformed":
        try:
            check_destination(args.group)
        except RefusedFrame as exc:
            ap.error(str(exc))
    entry = None
    if args.node:
        entry = load_fleet()["nodes"].get(args.node)
    if entry is not None:
        args.name = args.name or entry["call"]
        if args.family is None:
            args.family = entry["family"]
    args.family = args.family or "any"
    if not (args.name or args.address):
        ap.error("pass --node, --name or --address")


async def run_command(args: argparse.Namespace, emit: Emit = say) -> Verdict:
    connector = bleak_connector(args, emit)
    try:
        return await COMMANDS[args.cmd](connector, args, emit, args.family)
    except Exception as exc:  # noqa: BLE001 - every failure becomes a FAIL line
        if _is_bluetooth_unavailable(exc):
            print(f"{exc}\n\n{PERMISSION_HELP}", file=sys.stderr)
        return Verdict(False, f"{args.cmd}: {type(exc).__name__}: {exc}")


def main(argv: Iterable[str] | None = None) -> int:
    ap = build_parser()
    args = ap.parse_args(list(argv) if argv is not None else None)
    if args.self_test:
        return _self_test()
    if not args.cmd:
        ap.error(
            "pass a subcommand (burst, pin, flood, settings-roundtrip, malformed) or --self-test"
        )
    prepare(ap, args)
    # Operator rule 2026-09-26: no test on a node without a valid identity,
    # checked over the text console before any BLE traffic.
    enforce(ap, args)
    verdict = asyncio.run(run_command(args))
    say(("PASS " if verdict.ok else "FAIL ") + verdict.summary)
    return 0 if verdict.ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
