#!/usr/bin/env python3
"""Offline tests for ble_stress.py: frame builders, guards, burst analysis, and
each subcommand against a fake node that models `readPhoneCommand()`.

Nothing here touches BLE, serial or the network: `ble_stress` imports bleak only
inside `bleak_connector()`, and the subcommands take a connector, which these
tests replace with `FakeNode.connect`. Run with either

    python3 tools/bench/test_ble_stress.py
    python3 -m unittest tools/bench/test_ble_stress.py -v
"""

from __future__ import annotations

import asyncio
import contextlib
import hashlib
import io
import sys
import unittest
from collections.abc import AsyncIterator, Callable
from pathlib import Path
from typing import Any
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))

import ble_stress as bs

# The tests must not wait for the real drain floors.
bs.FLOOD_QUIET_MIN = 0.05
bs.DRAIN_BASE = 0.3
bs.DRAIN_PER_FRAME = 0.0

CORPUS = (
    Path(__file__).resolve().parents[2] / "test" / "test_aprs_corpus" / "corpus.txt"
)


def corpus_frame(name: str) -> bytes:
    for line in CORPUS.read_text().splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            key, hexstr = line.split()
            if key == name:
                return bytes.fromhex(hexstr)
    raise KeyError(name)


# --------------------------------------------------------------- fake node


class FakeTransport:
    def __init__(self, node: FakeNode) -> None:
        self.node = node
        self.connected = True
        self.muted = False
        self.ready = False
        self._cb: Callable[[bytes], None] | None = None

    @property
    def is_connected(self) -> bool:
        return self.connected

    @property
    def mtu(self) -> int:
        return 247

    def set_notify(self, callback: Callable[[bytes], None]) -> None:
        self._cb = callback

    def notify(self, frame: bytes) -> None:
        if self._cb is not None and self.connected and not self.muted:
            self._cb(frame)

    async def write(self, frame: bytes, response: bool) -> None:
        if not self.connected:
            raise ConnectionError("link down")
        self.node.on_write(self, frame)


class FakeNode:
    """A node as `readPhoneCommand()` and the command builders see the link."""

    def __init__(
        self,
        *,
        pin: int | None = None,
        family: str = "esp32",
        symbol: tuple[str, str] = ("/", "#"),
        drop_typs: tuple[str, ...] = (),
        drop_fields: dict[str, list[str]] | None = None,
        kill_on: Callable[[bytes], bool] | None = None,
        hang_on: Callable[[bytes], bool] | None = None,
        drop_every: int = 0,
        mutate_on_symbol: bool = False,
        broadcast_frame: bytes | None = None,
        msgid: int = 100,
    ) -> None:
        self.pin, self.family, self.symbol = pin, family, symbol
        self.drop_typs = drop_typs
        self.drop_fields = drop_fields or {}
        self.kill_on, self.hang_on = kill_on, hang_on
        self.drop_every = drop_every
        self.mutate_on_symbol = mutate_on_symbol
        self.broadcast_frame = broadcast_frame
        self.msgid = msgid
        self.writes: list[bytes] = []
        self.texts: list[bytes] = []
        self.commands = 0
        self.gw = True
        self.connections = 0

    @contextlib.asynccontextmanager
    async def connect(self) -> AsyncIterator[FakeTransport]:
        tr = FakeTransport(self)
        self.connections += 1
        try:
            yield tr
        finally:
            tr.connected = False

    def register(self, typ: str) -> bytes:
        obj: dict[str, Any] = {"TYP": typ}
        for name in bs.REGISTER_FIELDS[typ]:
            obj[name] = 0
        if typ == "SA":
            obj["SYMID"], obj["SYMCD"] = self.symbol
        if typ == "I":
            obj["BPIN"] = self.pin or 0
            obj["BATV"] = 4.0 + 0.001 * self.commands  # live reading, must be masked
        if typ == "SN":
            obj["GW"] = self.gw
        for name in self.drop_fields.get(typ, []):
            del obj[name]
        return bs.json_frame(obj)

    def send_typs(self, tr: FakeTransport, typs: tuple[str, ...]) -> None:
        for typ in typs:
            if typ not in self.drop_typs:
                tr.notify(self.register(typ))

    def on_write(self, tr: FakeTransport, frame: bytes) -> None:
        self.writes.append(frame)
        if self.kill_on is not None and self.kill_on(frame):
            tr.connected = False
            return
        if self.hang_on is not None and self.hang_on(frame):
            tr.muted = True
            return
        msg_len, op = frame[0], frame[1]
        data = frame + bytes(300)  # the queue item is zero-initialised
        if op == 0x10:
            if data[2:4] != b"\x20\x30":
                return
            if self.pin is not None:
                good = hashlib.sha256(f"{self.pin:06d}".encode()).digest()
                if msg_len < 35 or data[4:36] != good:
                    tr.connected = False
                    return
            tr.ready = True
            self.burst(tr)
        elif op == 0xA0 and tr.ready:
            if msg_len < 2:
                return
            text = data[2:msg_len].split(b"\x00")[0]
            if text.startswith(b"--"):
                self.command(tr, text.decode())
            else:
                # the firmware prepends one colon: `:{9}x` becomes `::{9}x`;
                # sendMessage() drops a `{dst}text` over 160 characters
                if len(text) - 1 <= bs.TEXT_MAX_CHARS:
                    self.texts.append(b":" + text)
                    if self.broadcast_frame is not None:
                        tr.notify(b"\x40" + self.broadcast_frame)
        elif op == 0x95 and tr.ready:
            if data[2] in (0x2F, 0x5C):
                self.symbol = (chr(data[2]), chr(data[3]))
            if self.mutate_on_symbol:
                self.gw = not self.gw
        elif op == 0xF0:
            tr.connected = False

    def burst(self, tr: FakeTransport) -> None:
        for typ in bs.expected_typs(self.family):
            self.send_typs(tr, (typ,))
        tr.notify(bs.json_frame({"TYP": "MH", "CALL": "DK5EN-1"}))
        self.send_typs(tr, ("CONFFIN",))

    def command(self, tr: FakeTransport, cmd: str) -> None:
        self.commands += 1
        if self.drop_every and self.commands % self.drop_every == 0:
            return  # the single-slot mailbox was overwritten
        if cmd == "--msgid":
            tr.notify(
                b"\x40:\x01\x02\x03\x04\x05response>*:"
                + f"--msgid {self.msgid}\n".encode()
            )
            return
        self.send_typs(tr, bs.COMMAND_REPLIES.get(cmd, ()))


def make_args(sub: str, *extra: str) -> Any:
    ap = bs.build_parser()
    args = ap.parse_args(
        [
            sub,
            "--no-identity-guard",
            "--name",
            "DK5EN-90",
            "--connect-wait",
            "0",
            "--timeout",
            "1",
            "--settle",
            "0.02",
            "--quiet",
            "0.05",
            "--drain",
            "0.2",
            "--probe-timeout",
            "0.4",
            "--gap",
            "0",
            *(["--reject-wait", "0.3"] if sub == "pin" else []),
            *extra,
        ]
    )
    bs.prepare(ap, args)
    return args


def run(cmd: str, node: FakeNode, *extra: str, family: str | None = None):
    args = make_args(cmd, *extra)
    lines: list[str] = []
    fam = family or args.family
    verdict = asyncio.run(bs.COMMANDS[cmd](node.connect, args, lines.append, fam))
    return verdict, lines


# --------------------------------------------------------------- builders


class TestFrames(unittest.TestCase):
    def test_group_text_is_single_colon(self) -> None:
        self.assertEqual(bs.build_group_text("9", "hi"), bytes([8, 0xA0]) + b":{9}hi")

    def test_dm_and_group_9999(self) -> None:
        self.assertEqual(bs.build_group_text("9999", "x")[2:], b":{9999}x")
        self.assertEqual(bs.build_group_text("dk5en-14", "x")[2:], b":{DK5EN-14}x")

    def test_text_limit(self) -> None:
        bs.build_group_text("9", "x" * (bs.TEXT_MAX_CHARS - 3))
        with self.assertRaises(bs.RefusedFrame):
            bs.build_group_text("9", "x" * (bs.TEXT_MAX_CHARS - 2))

    def test_symbol_and_save(self) -> None:
        self.assertEqual(bs.build_symbol("\\", "-"), bytes([4, 0x95, 0x5C, 0x2D]))
        self.assertEqual(bs.build_save(), bytes([2, 0xF0]))

    def test_malformed_frames_shape(self) -> None:
        by = {m.name: m.frame for m in bs.malformed_frames()}
        self.assertEqual(
            set(by),
            {
                "len-too-big",
                "len-zero",
                "len-one",
                "truncated-hello",
                "oversized-text",
                "unknown-opcode",
            },
        )
        self.assertGreater(by["len-too-big"][0], len(by["len-too-big"]))
        self.assertEqual(by["len-zero"][0], 0)
        self.assertEqual(by["len-one"][0], 1)
        self.assertEqual(by["truncated-hello"], bytes([4, 0x10]))
        over = by["oversized-text"]
        self.assertEqual(over[0], len(over))
        self.assertGreater(len(over) - 2, bs.TEXT_MAX_CHARS)
        self.assertTrue(over[2:].startswith(b":{9}"))
        self.assertNotIn(b"::", over)

    def test_every_malformed_frame_passes_the_guard(self) -> None:
        for mf in bs.malformed_frames("9999"):
            bs.guard_frame(mf.frame)

    def test_hello_log_never_shows_the_hash(self) -> None:
        frame = bs.build_hello(123456)
        shown = bs.frame_repr(frame)
        self.assertIn("<pin-hash-32>", shown)
        self.assertNotIn(frame[4:].hex(), shown)
        self.assertEqual(bs.frame_repr(bs.build_hello(None)), "04102030")


class TestGuard(unittest.TestCase):
    def assertRefused(self, frame: bytes, **kw: Any) -> None:
        with self.assertRaises(bs.RefusedFrame):
            bs.guard_frame(frame, **kw)

    def test_destinations(self) -> None:
        for good in ("9", "9999", "DK5EN-1", "DK5EN-90", "dk5en-14", "DK5EN-100"):
            bs.check_destination(good)
        for bad in ("*", "", "all", "OE1XAR-62", "DK5EN", "DK5EN-1234", "99", "{9}"):
            with self.assertRaises(bs.RefusedFrame, msg=bad):
                bs.check_destination(bad)

    def test_broadcast_shapes_refused(self) -> None:
        self.assertRefused(bs.build_text("hello"))  # bare text
        self.assertRefused(bs.build_text("::{9}hi"))  # the 2026-09-11 form
        self.assertRefused(bs.build_text(":hi"))
        self.assertRefused(bs.build_text(":{*}hi"))
        self.assertRefused(bs.build_text(":{OE1XAR-62}hi"))
        self.assertRefused(bs.build_text(":{ 9 }hi"))
        self.assertRefused(bs.build_text(":{dk5en-14}hi"))

    def test_only_read_only_commands(self) -> None:
        for cmd in bs.COMMAND_REPLIES:
            bs.guard_frame(bs.build_text(cmd))
        self.assertRefused(bs.build_text("--mesh off"))
        self.assertRefused(bs.build_text("--setcall XX0XXX-00"))
        self.assertRefused(bs.build_text("--info "))
        with self.assertRaises(bs.RefusedFrame):
            bs.build_command("--reboot")

    def test_settings_opcodes(self) -> None:
        for op in (0x50, 0x55, 0x70, 0x80, 0x90):
            self.assertRefused(bytes([7, op, 0, 0, 0, 0, 0x0B]), allow_save=True)
        self.assertRefused(bytes([4, 0xEE, 0, 0]))  # unknown high opcode
        self.assertRefused(bs.build_symbol("/", "#"))
        bs.guard_frame(bs.build_symbol("/", "#"), allow_settings=frozenset({0x95}))
        self.assertRefused(bs.build_save())
        bs.guard_frame(bs.build_save(), allow_save=True)

    def test_session_refuses_before_writing(self) -> None:
        node = FakeNode()

        async def go() -> None:
            async with node.connect() as tr:
                s = bs.Session(tr, lambda _t: None)
                with self.assertRaises(bs.RefusedFrame):
                    await s.send(bs.build_text("hello"), "bare")
                with self.assertRaises(bs.RefusedFrame):
                    await s.send(bs.build_save(), "save")

        asyncio.run(go())
        self.assertEqual(node.writes, [])


# --------------------------------------------------------------- analysis


class TestAnalysis(unittest.TestCase):
    def burst(self, family: str = "esp32") -> list[bytes]:
        node = FakeNode(family=family)
        frames = [node.register(t) for t in bs.expected_typs(family)]
        frames.insert(3, bs.json_frame({"TYP": "MH", "CALL": "X"}))
        frames.append(node.register("CONFFIN"))
        return frames

    def test_expected_typ_lists(self) -> None:
        self.assertEqual(
            bs.expected_typs("nrf52"),
            list(bs._fields("I IS1 SE S1 SW S2 SN SN1 W G SA IO TM")),
        )
        self.assertEqual(bs.expected_typs("esp32")[-1], "AN")
        self.assertNotIn("AN", bs.expected_typs("any"))

    def test_sn_sn1_fields_from_the_builders(self) -> None:
        self.assertEqual(
            bs.REGISTER_FIELDS["SN"],
            bs._fields(
                "GW WS DISP BTN MSH GPS TRACK UTCOF TXP MQRG MSF MCR MBW GWNPOS "
                "NOALL NOPMOTHER BLED GWS"
            ),
        )
        self.assertEqual(bs.REGISTER_FIELDS["SN1"], ("VIA", "VIACALL", "WSPWD", "ASYM"))

    def test_complete_burst(self) -> None:
        rep = bs.analyse_burst(self.burst(), "esp32")
        self.assertTrue(rep.ok, rep.problems())
        self.assertEqual(rep.mh_frames, 1)
        self.assertEqual(rep.warnings, [])

    def test_missing_register_and_conffin(self) -> None:
        frames = [f for f in self.burst() if bs.register_of(f) != "SN1"]
        rep = bs.analyse_burst(frames, "esp32")
        self.assertEqual(rep.missing, ["SN1"])
        self.assertFalse(rep.ok)
        self.assertFalse(bs.analyse_burst(self.burst()[:-1], "esp32").ok)

    def test_an_only_on_esp32(self) -> None:
        frames = [f for f in self.burst() if bs.register_of(f) != "AN"]
        self.assertEqual(bs.analyse_burst(frames, "esp32").missing, ["AN"])
        self.assertTrue(bs.analyse_burst(frames, "nrf52").ok)
        self.assertTrue(bs.analyse_burst(frames, "any").ok)

    def test_dropped_trailing_field(self) -> None:
        node = FakeNode(drop_fields={"SN": ["GWS"]})
        frames = [node.register(t) for t in bs.expected_typs("esp32")]
        frames.append(node.register("CONFFIN"))
        rep = bs.analyse_burst(frames, "esp32")
        self.assertEqual(rep.missing_fields, {"SN": ["GWS"]})
        self.assertFalse(rep.ok)

    def test_oversize_and_invalid_json(self) -> None:
        frames = self.burst()
        frames[0] = bs.json_frame({"TYP": "I", "PAD": "x" * 300})
        self.assertTrue(bs.analyse_burst(frames, "esp32").bad_frames)
        frames = self.burst()
        frames[1] = b"\x44{broken\x00\x00"
        self.assertTrue(bs.analyse_burst(frames, "esp32").bad_frames)

    def test_frame_size_boundary(self) -> None:
        def frame_of(size: int) -> bytes:
            pad = (
                size - len(bs.json_frame({"TYP": "IS1", "BDATE": ""}).rstrip(b"\0")) + 1
            )
            return bs.json_frame({"TYP": "IS1", "BDATE": "x" * pad})

        ok = frame_of(bs.BLE_JSON_PAYLOAD_MAX)
        self.assertEqual(bs.decode_json_frame(ok).json_len, bs.BLE_JSON_PAYLOAD_MAX)
        self.assertFalse(bs.analyse_burst([ok], "none").bad_frames)
        over = frame_of(bs.BLE_JSON_PAYLOAD_MAX + 1)
        self.assertTrue(bs.analyse_burst([over], "none").bad_frames)

    def test_duplicate_and_order_are_warnings(self) -> None:
        frames = self.burst()
        frames.insert(-1, frames[0])
        rep = bs.analyse_burst(frames, "esp32")
        self.assertTrue(rep.ok)
        self.assertTrue(any("duplicate" in w for w in rep.warnings))

    def test_redaction_and_diff(self) -> None:
        self.assertEqual(
            bs.redact_obj({"BPIN": 1, "WSPWD": "s", "CALL": "X"}),
            {"BPIN": "<redacted>", "WSPWD": "<redacted>", "CALL": "X"},
        )
        diffs = bs.diff_registers(
            {"SN": {"GW": 1, "WSPWD": "a"}, "I": {"BATV": 4.1}},
            {"SN": {"GW": 0, "WSPWD": "b"}, "I": {"BATV": 3.9}},
        )
        self.assertEqual(diffs, ["SN.GW: 1 -> 0", "SN.WSPWD: <redacted> -> <redacted>"])

    def test_msgid(self) -> None:
        self.assertEqual(bs.msgid_of(b"\x40:xx response>*:--msgid 77\n"), 77)
        self.assertIsNone(bs.msgid_of(b"\x44{}"))

    def test_symbol_from_register(self) -> None:
        self.assertEqual(
            bs.symbol_frame_from_register({"SYMID": "/", "SYMCD": "#"}),
            bs.build_symbol("/", "#"),
        )
        for bad in ({"SYMID": "X", "SYMCD": "#"}, {"SYMID": "/", "SYMCD": ""}):
            with self.assertRaises(ValueError):
                bs.symbol_frame_from_register(bad)
        with self.assertRaises(TypeError):
            bs.symbol_frame_from_register({})


# --------------------------------------------------------------- burst


class TestBurstCommand(unittest.TestCase):
    def test_pass_over_two_cycles(self) -> None:
        node = FakeNode()
        verdict, lines = run("burst", node, "--cycles", "2", "--family", "esp32")
        self.assertTrue(verdict.ok, verdict.summary)
        self.assertEqual(node.connections, 2)
        self.assertEqual(sum("PASS frames=" in ln for ln in lines), 2)
        self.assertIn("missing registers total: none", verdict.summary)
        # only the open hello was written: no timesync unless asked for
        self.assertEqual(node.writes, [bytes.fromhex("04102030")] * 2)

    def test_missing_register_is_totalled(self) -> None:
        node = FakeNode(drop_typs=("SN1",))
        verdict, _ = run("burst", node, "--cycles", "2", "--family", "esp32")
        self.assertFalse(verdict.ok)
        self.assertIn("SN1x2", verdict.summary)

    def test_mtu_flag(self) -> None:
        _, lines = run("burst", FakeNode(), "--mtu")
        self.assertTrue(any("mtu=247" in ln for ln in lines))

    def test_pin_never_in_log(self) -> None:
        node = FakeNode(pin=123456)
        _, lines = run("burst", node, "--pin", "123456")
        blob = "\n".join(lines)
        self.assertNotIn("123456", blob)
        self.assertNotIn(hashlib.sha256(b"123456").hexdigest(), blob)


# --------------------------------------------------------------- pin


class TestPinCommand(unittest.TestCase):
    def test_no_pin_node(self) -> None:
        verdict, _ = run("pin", FakeNode(), "--expect-pin", "none")
        self.assertTrue(verdict.ok, verdict.summary)

    def test_node_with_pin_but_expected_none(self) -> None:
        verdict, _ = run("pin", FakeNode(pin=123456), "--expect-pin", "none")
        self.assertFalse(verdict.ok)

    def test_pin_node(self) -> None:
        node = FakeNode(pin=123456)
        verdict, lines = run("pin", node, "--expect-pin", "set", "--pin", "123456")
        self.assertTrue(verdict.ok, verdict.summary)
        self.assertIn("3/3 scenarios", verdict.summary)
        self.assertEqual(node.connections, 3)
        # the wrong hash really was wrong, and was not the right one
        hellos = [w for w in node.writes if w[1] == 0x10]
        self.assertEqual(len({h[4:] for h in hellos if len(h) > 4}), 2)
        self.assertNotIn("123456", "\n".join(lines))

    def test_wrong_pin_given(self) -> None:
        node = FakeNode(pin=123456)
        verdict, _ = run("pin", node, "--expect-pin", "set", "--pin", "111111")
        self.assertFalse(verdict.ok)

    def test_node_that_ignores_the_pin_fails(self) -> None:
        verdict, _ = run("pin", FakeNode(), "--expect-pin", "set", "--pin", "123456")
        self.assertFalse(verdict.ok)
        self.assertIn("failed", verdict.summary)


# --------------------------------------------------------------- flood


class TestFlood(unittest.TestCase):
    def test_text_flood_group_9(self) -> None:
        node = FakeNode()
        verdict, _ = run("flood", node, "--count", "5")
        self.assertTrue(verdict.ok, verdict.summary)
        self.assertEqual(len(node.texts), 5)
        self.assertTrue(all(t.startswith(b"::{9}ble-stress ") for t in node.texts))
        self.assertTrue(all(len(t) <= bs.TEXT_MAX_CHARS + 1 for t in node.texts))

    def test_dm_flood(self) -> None:
        node = FakeNode()
        verdict, _ = run("flood", node, "--count", "2", "--dm", "DK5EN-14")
        self.assertTrue(verdict.ok, verdict.summary)
        self.assertTrue(all(t.startswith(b"::{DK5EN-14}") for t in node.texts))

    def test_destination_refused_at_parse_time(self) -> None:
        for extra in (["--group", "1"], ["--dm", "OE1XAR-62"], ["--dm", "*"]):
            with (
                self.subTest(extra=extra),
                contextlib.redirect_stderr(io.StringIO()),
                self.assertRaises(SystemExit),
            ):
                make_args("flood", *extra)

    def test_count_bounds(self) -> None:
        for count in ("0", str(bs.FLOOD_MAX + 1)):
            with (
                contextlib.redirect_stderr(io.StringIO()),
                self.assertRaises(SystemExit),
            ):
                make_args("flood", "--count", count)

    def test_broadcast_on_air_fails_the_flood(self) -> None:
        node = FakeNode(broadcast_frame=corpus_frame("f011"))
        verdict, lines = run("flood", node, "--count", "2", "--own-call", "DK5EN-91")
        self.assertFalse(verdict.ok)
        self.assertIn("BROADCAST", verdict.summary)
        self.assertTrue(any("BROADCAST" in ln for ln in lines))

    def test_foreign_broadcast_is_not_ours(self) -> None:
        node = FakeNode(broadcast_frame=corpus_frame("f011"))
        verdict, _ = run("flood", node, "--count", "2", "--own-call", "DK5EN-93")
        self.assertTrue(verdict.ok, verdict.summary)

    def test_command_flood_counts_replies(self) -> None:
        node = FakeNode()
        verdict, _ = run("flood", node, "--commands", "--rounds", "5")
        self.assertTrue(verdict.ok, verdict.summary)
        self.assertIn("25/25", verdict.summary)  # (I, IS1, G, SE, S1) x 5
        self.assertEqual(node.commands, 15)

    def test_command_flood_reports_lost_replies(self) -> None:
        node = FakeNode(drop_every=4)
        verdict, lines = run("flood", node, "--commands", "--rounds", "8")
        self.assertFalse(verdict.ok)
        self.assertIn("replies missing", verdict.summary)
        self.assertTrue(any(ln.count("MISSING") for ln in lines))

    def test_command_flood_refuses_write_commands(self) -> None:
        with (
            contextlib.redirect_stderr(io.StringIO()),
            self.assertRaises(SystemExit),
        ):
            make_args("flood", "--commands", "--cmd", "mesh")

    def test_custom_command_list(self) -> None:
        node = FakeNode()
        verdict, _ = run(
            "flood",
            node,
            "--commands",
            "--rounds",
            "3",
            "--cmd",
            "wx",
            "--cmd=--tel",
        )
        self.assertTrue(verdict.ok, verdict.summary)
        self.assertIn("6/6", verdict.summary)


# --------------------------------------------------------------- roundtrip


class TestRoundtrip(unittest.TestCase):
    def test_identical_write_passes(self) -> None:
        node = FakeNode(symbol=("\\", "-"))
        verdict, lines = run("settings-roundtrip", node)
        self.assertTrue(verdict.ok, verdict.summary)
        ops = [w[1] for w in node.writes]
        self.assertIn(0x95, ops)
        self.assertNotIn(0xF0, ops)
        self.assertEqual(
            [w for w in node.writes if w[1] == 0x95], [bs.build_symbol("\\", "-")]
        )
        self.assertEqual(node.symbol, ("\\", "-"))
        self.assertTrue(any(ln.endswith("MARK after-write") for ln in lines))
        self.assertTrue(any("msgid before=100" in ln for ln in lines))
        self.assertTrue(any("msgid after=100" in ln for ln in lines))

    def test_only_harmless_opcodes(self) -> None:
        node = FakeNode()
        run("settings-roundtrip", node)
        for w in node.writes:
            self.assertIn(w[1], (0x10, 0xA0, 0x95))
            if w[1] == 0xA0:
                self.assertIn(w[2:].decode(), bs.COMMAND_REPLIES)

    def test_allow_save_sends_0xf0(self) -> None:
        node = FakeNode()
        verdict, lines = run("settings-roundtrip", node, "--allow-save")
        self.assertTrue(verdict.ok, verdict.summary)
        self.assertEqual(node.writes[-1], bs.build_save())
        self.assertTrue(any("WARNING --allow-save" in ln for ln in lines))
        self.assertIn("0xF0 sent", verdict.summary)

    def test_a_value_that_changed_fails(self) -> None:
        node = FakeNode(mutate_on_symbol=True)
        verdict, lines = run("settings-roundtrip", node)
        self.assertFalse(verdict.ok)
        self.assertTrue(any("DIFF SN.GW" in ln for ln in lines))

    def test_unwritable_symbol_sends_nothing(self) -> None:
        node = FakeNode(symbol=("X", "#"))
        verdict, _ = run("settings-roundtrip", node)
        self.assertFalse(verdict.ok)
        self.assertFalse([w for w in node.writes if w[1] == 0x95])

    def test_live_readings_do_not_count_as_changes(self) -> None:
        node = FakeNode()  # I.BATV moves with every command
        verdict, lines = run("settings-roundtrip", node)
        self.assertTrue(verdict.ok, verdict.summary)
        self.assertFalse(any("BATV" in ln for ln in lines))

    def test_sensitive_values_never_logged(self) -> None:
        node = FakeNode(pin=654321)
        _, lines = run("settings-roundtrip", node, "--pin", "654321")
        self.assertNotIn("654321", "\n".join(lines))


# --------------------------------------------------------------- malformed


class TestMalformed(unittest.TestCase):
    def test_robust_node_survives_everything(self) -> None:
        node = FakeNode()
        verdict, lines = run("malformed", node)
        self.assertTrue(verdict.ok, verdict.summary)
        self.assertIn("6/6", verdict.summary)
        self.assertEqual(node.connections, 1)
        sent = [f.frame for f in bs.malformed_frames()]
        for frame in sent:
            self.assertIn(frame, node.writes)
        # len-too-big really did read as `--info` on the node (replies came back)
        self.assertTrue(any("len-too-big OK (replies I,IS1)" in ln for ln in lines))
        self.assertEqual(node.texts, [])  # nothing reached the air

    def test_a_frame_that_kills_the_link_is_named(self) -> None:
        bad = bs.malformed_frames()[3].frame  # truncated-hello
        node = FakeNode(kill_on=lambda f: f == bad)
        verdict, _ = run("malformed", node)
        self.assertFalse(verdict.ok)
        self.assertIn("truncated-hello KILLED", verdict.summary)
        self.assertNotIn("len-zero", verdict.summary)
        self.assertGreaterEqual(node.connections, 2)  # it came back for the rest

    def test_a_frame_that_hangs_the_link_is_named(self) -> None:
        bad = bs.malformed_frames()[1].frame  # len-zero
        node = FakeNode(hang_on=lambda f: f == bad)
        verdict, _ = run("malformed", node)
        self.assertFalse(verdict.ok)
        self.assertIn("len-zero HUNG", verdict.summary)
        self.assertIn("5/6", verdict.summary)

    def test_skip(self) -> None:
        node = FakeNode()
        verdict, _ = run("malformed", node, "--skip", "oversized-text")
        self.assertIn("5/5", verdict.summary)


# --------------------------------------------------------------- cli


class TestCli(unittest.TestCase):
    def call_main(self, node: FakeNode, *argv: str) -> tuple[int, str]:
        out = io.StringIO()
        with (
            mock.patch.object(bs, "bleak_connector", lambda _a, _e: node.connect),
            mock.patch.object(bs, "enforce", lambda _ap, _a: None),
            contextlib.redirect_stdout(out),
        ):
            rc = bs.main(
                [
                    argv[0],
                    "--name",
                    "DK5EN-90",
                    "--connect-wait",
                    "0",
                    "--timeout",
                    "1",
                    *argv[1:],
                ]
            )
        return rc, out.getvalue()

    def test_self_test(self) -> None:
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            rc = bs.main(["--self-test"])
        self.assertEqual(rc, 0)
        self.assertIn("PASS self-test", out.getvalue())

    def test_exit_code_and_summary_line(self) -> None:
        rc, out = self.call_main(FakeNode(), "burst", "--family", "esp32")
        self.assertEqual(rc, 0)
        self.assertTrue(
            out.strip().splitlines()[-1].split("  ", 1)[1].startswith("PASS burst")
        )
        rc, out = self.call_main(
            FakeNode(drop_typs=("SN",)), "burst", "--family", "esp32"
        )
        self.assertEqual(rc, 1)
        self.assertTrue(
            out.strip().splitlines()[-1].split("  ", 1)[1].startswith("FAIL burst")
        )

    def test_every_line_has_a_wall_clock_stamp(self) -> None:
        _, out = self.call_main(FakeNode(), "burst", "--family", "esp32")
        for line in out.strip().splitlines():
            self.assertRegex(line, r"^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d{3}  ")

    def test_usage_errors(self) -> None:
        with contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):
                bs.main([])  # no subcommand
            with self.assertRaises(SystemExit):
                bs.main(["pin", "--name", "X", "--expect-pin", "set"])  # no PIN
            with self.assertRaises(SystemExit):
                bs.main(["burst"])  # no target

    def test_pin_file(self) -> None:
        import tempfile

        with tempfile.NamedTemporaryFile("w", suffix=".pin", delete=False) as fh:
            fh.write("123456\n")
            path = Path(fh.name)
        try:
            args = make_args("burst", "--pin-file", str(path))
            self.assertEqual(args.pin_value, 123456)
        finally:
            path.unlink()


if __name__ == "__main__":
    unittest.main()
