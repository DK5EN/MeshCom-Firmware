#!/usr/bin/env python3
"""Unit tests for the `{pong}`-to-BLE regression check (pong_ble_check.py).

Host-side, hardware-free: `pong_ble_check.py` only imports `bleak` inside
`run()` (same property as `ble_golden.py`), so importing the module here
never touches BLE. Run with either

    python3 -m pytest tools/bench/test_pong_ble_check.py -q
    python3 -m unittest tools/bench/test_pong_ble_check.py -v
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "test" / "golden"))

import mc_frame  # noqa: E402  (path insert must come first)
import pong_ble_check as pbc  # noqa: E402  (path insert must come first)

HEX_DIGITS = set("0123456789abcdefABCDEF")


def realistic_notify(frame: bytes, unix_time: int = 0x11223344) -> bytes:
    """The actual BLE wire shape of a text/pos notification, not a bare frame.

    `addBLEOutBuffer()` (src/loop_functions.cpp ~696) appends a 4-byte
    big-endian unix timestamp after the LoRa frame for every non-JSON
    message; `sendToPhone()` (src/phone_commands.cpp) then prepends the 0x40
    tag and sends `blelen + 2` bytes -- two trailing filler bytes from the
    zero-initialized send buffer. A matcher that only works against a bare
    `0x40 + frame` (no trailer) would not be proven against what the node
    actually puts on the wire.
    """
    return bytes([0x40]) + frame + unix_time.to_bytes(4, "big") + b"\x00\x00"


# --------------------------------------------------------------- frame builder


class TestBuildFrame(unittest.TestCase):
    """`build_frame()` must reproduce the wire format byte for byte."""

    def test_reproduces_corpus_f002(self):
        corpus = (Path(__file__).resolve().parents[2]
                  / "test" / "test_aprs_corpus" / "corpus.txt")
        f002 = None
        for line in corpus.read_text().splitlines():
            line = line.strip()
            if line.startswith("f002 "):
                f002 = bytes.fromhex(line.split()[1])
        self.assertIsNotNone(f002)

        rebuilt = pbc.build_frame(
            msg_id=0x6A8825A4,
            flags=0xB0,
            path=["OE1XAR-62", "DK4YU-77", "DL2JA-2", "DK5EN-91"],
            dest=b"*",
            payload=b"{CET}2026-08-21 10:57:28",
            hw=0x00,
            mod=0x88,
            trailer=bytes.fromhex("00ab237e"),
        )
        self.assertEqual(rebuilt, f002)

    def test_fcs_is_the_plain_byte_sum_mod_0x10000(self):
        frame = pbc.build_frame(
            msg_id=1, flags=0x04, path=["DK5EN-97"], dest=b"DK5EN-93",
            payload=b"pbcdeadbeef",
        )
        # Locate the FCS: it sits right before the 4-byte trailer.
        head = frame[:-6]
        fcs_bytes = frame[-6:-4]
        expected = sum(head) & 0xFFFF
        self.assertEqual(int.from_bytes(fcs_bytes, "big"), expected)

    def test_msg_id_is_little_endian(self):
        frame = pbc.build_frame(
            msg_id=0x11223344, flags=0x04, path=["DK5EN-97"], dest=b"DK5EN-93",
            payload=b"x",
        )
        self.assertEqual(frame[1:5], bytes([0x44, 0x33, 0x22, 0x11]))

    def test_path_and_dest_and_colon_layout(self):
        frame = pbc.build_frame(
            msg_id=1, flags=0x04, path=["DK5EN-97"], dest=b"DK5EN-93",
            payload=b"hello",
        )
        # type(1) + msg_id(4) + flags(1) = 6 bytes head, then ASCII.
        text = frame[6:]
        self.assertTrue(text.startswith(b"DK5EN-97>DK5EN-93"))
        # The destination terminator is the type byte again -- 0x3A is ':'.
        after_dest = text[len(b"DK5EN-97>DK5EN-93"):]
        self.assertEqual(after_dest[0], pbc.FRAME_TYPE_TEXT)
        self.assertEqual(chr(pbc.FRAME_TYPE_TEXT), ":")
        self.assertEqual(after_dest[1:1 + len(b"hello")], b"hello")


# --------------------------------------------------------------- flags constants


class TestProbeFlags(unittest.TestCase):
    """Server bit always on, hop nibble always 0 -- not options, constants."""

    def test_server_bit_always_set(self):
        self.assertEqual(pbc.PROBE_FLAGS & 0x80, 0x80)

    def test_hop_nibble_always_zero(self):
        self.assertEqual(pbc.PROBE_FLAGS & 0x0F, 0x00)
        self.assertEqual(pbc.FLAGS_DIRECT_BASE, 0x00)


# --------------------------------------------------------------- probe builders


class TestControlProbe(unittest.TestCase):
    def test_server_bit_always_set(self):
        frame, token = pbc.build_control_probe("DK5EN-97", "DK5EN-93")
        self.assertEqual(frame[5] & 0x80, 0x80)

    def test_hop_nibble_always_zero(self):
        frame, token = pbc.build_control_probe("DK5EN-97", "DK5EN-93")
        self.assertEqual(frame[5] & 0x0F, 0x00)

    def test_no_open_brace_in_control_payload(self):
        for _ in range(50):
            _frame, token = pbc.build_control_probe("DK5EN-97", "DK5EN-93")
            self.assertNotIn("{", token)
            self.assertTrue(token.startswith(pbc.CONTROL_PREFIX))

    def test_token_has_a_non_hex_character(self):
        # 'p' in the "pbc" prefix is not a hex digit -- see build_pong_probe()'s
        # docstring for why this matters (a hex-only token could match its own
        # echoed --injectraw command on a non-instrumented image).
        for _ in range(50):
            _frame, token = pbc.build_control_probe("DK5EN-97", "DK5EN-93")
            self.assertTrue(any(c not in HEX_DIGITS for c in token))

    def test_token_cannot_appear_inside_the_frames_own_hex_encoding(self):
        for _ in range(20):
            frame, token = pbc.build_control_probe("DK5EN-97", "DK5EN-93")
            self.assertNotIn(token, frame.hex())

    def test_fcs_correct(self):
        frame, _token = pbc.build_control_probe("DK5EN-97", "DK5EN-93",
                                                  msg_id=0xAABBCCDD)
        head = frame[:-6]
        fcs_bytes = frame[-6:-4]
        self.assertEqual(int.from_bytes(fcs_bytes, "big"), sum(head) & 0xFFFF)

    def test_path_and_colon_layout(self):
        frame, token = pbc.build_control_probe("DK5EN-97", "DK5EN-93",
                                                 msg_id=1)
        text = frame[6:]
        self.assertTrue(text.startswith(b"DK5EN-97>DK5EN-93"))
        idx = len(b"DK5EN-97>DK5EN-93")
        self.assertEqual(text[idx], pbc.FRAME_TYPE_TEXT)   # ':' terminator
        self.assertEqual(text[idx + 1:idx + 1 + len(token)], token.encode("ascii"))

    def test_token_is_findable_by_the_wait_matcher(self):
        frame, token = pbc.build_control_probe("DK5EN-97", "DK5EN-93")
        notify = realistic_notify(frame)
        self.assertTrue(pbc.frame_contains_token(notify, token))

    def test_own_call_is_uppercased(self):
        frame, _token = pbc.build_control_probe("DK5EN-97", "dk5en-93", msg_id=1)
        text = frame[6:]
        self.assertTrue(text.startswith(b"DK5EN-97>DK5EN-93"))
        self.assertNotIn(b"dk5en-93", text)


class TestPongProbe(unittest.TestCase):
    def test_server_bit_always_set(self):
        frame, token = pbc.build_pong_probe("DK5EN-97", "DK5EN-93")
        self.assertEqual(frame[5] & 0x80, 0x80)

    def test_hop_nibble_always_zero(self):
        frame, token = pbc.build_pong_probe("DK5EN-97", "DK5EN-93")
        self.assertEqual(frame[5] & 0x0F, 0x00)

    def test_token_has_a_non_hex_character(self):
        # '{', '}', and the letters of "pong" are all non-hex.
        for _ in range(50):
            _frame, token = pbc.build_pong_probe("DK5EN-97", "DK5EN-93")
            self.assertTrue(any(c not in HEX_DIGITS for c in token))

    def test_token_cannot_appear_inside_the_frames_own_hex_encoding(self):
        for _ in range(20):
            frame, token = pbc.build_pong_probe("DK5EN-97", "DK5EN-93")
            self.assertNotIn(token, frame.hex())

    def test_token_shape(self):
        for _ in range(50):
            _frame, token = pbc.build_pong_probe("DK5EN-97", "DK5EN-93")
            self.assertTrue(token.startswith("{pong}{"))
            self.assertTrue(token.endswith("}"))
            digits = token[len("{pong}{"):-1]
            self.assertTrue(digits.isdigit())
            self.assertGreaterEqual(len(digits), 1)
            self.assertLessEqual(len(digits), 10)

    def test_fcs_correct(self):
        frame, _token = pbc.build_pong_probe("DK5EN-97", "DK5EN-93",
                                              msg_id=0x01020304)
        head = frame[:-6]
        fcs_bytes = frame[-6:-4]
        self.assertEqual(int.from_bytes(fcs_bytes, "big"), sum(head) & 0xFFFF)

    def test_path_and_colon_layout(self):
        frame, token = pbc.build_pong_probe("DK5EN-97", "DK5EN-93", msg_id=1)
        text = frame[6:]
        self.assertTrue(text.startswith(b"DK5EN-97>DK5EN-93"))
        idx = len(b"DK5EN-97>DK5EN-93")
        self.assertEqual(text[idx], pbc.FRAME_TYPE_TEXT)
        self.assertEqual(text[idx + 1:idx + 1 + len(token)], token.encode("ascii"))

    def test_token_is_findable_by_the_wait_matcher(self):
        frame, token = pbc.build_pong_probe("DK5EN-97", "DK5EN-93")
        notify = realistic_notify(frame)
        self.assertTrue(pbc.frame_contains_token(notify, token))

    def test_own_call_is_uppercased(self):
        frame, _token = pbc.build_pong_probe("DK5EN-97", "dk5en-93", msg_id=1)
        text = frame[6:]
        self.assertTrue(text.startswith(b"DK5EN-97>DK5EN-93"))
        self.assertNotIn(b"dk5en-93", text)

    def test_fresh_msg_id_each_call(self):
        # The dedup ring would swallow a repeat msg_id; builders must not
        # reuse one across calls when the caller does not pin it.
        ids = set()
        for _ in range(20):
            frame, _token = pbc.build_pong_probe("DK5EN-97", "DK5EN-93")
            ids.add(int.from_bytes(frame[1:5], "little"))
        self.assertGreater(len(ids), 1)


# --------------------------------------------------------------- frame_contains_token


class TestFrameContainsToken(unittest.TestCase):
    def test_non_0x40_frame_never_matches(self):
        self.assertFalse(pbc.frame_contains_token(bytes([0x44]) + b'{"TYP":"I"}',
                                                    "pbcdeadbeef"))

    def test_empty_frame_never_matches(self):
        self.assertFalse(pbc.frame_contains_token(b"", "pbcdeadbeef"))

    def test_unrelated_payload_does_not_match(self):
        frame, _token = pbc.build_control_probe("DK5EN-97", "DK5EN-93")
        notify = bytes([0x40]) + frame
        self.assertFalse(pbc.frame_contains_token(notify, "not-the-token"))

    def test_short_garbage_after_0x40_does_not_crash(self):
        self.assertFalse(pbc.frame_contains_token(b"\x40\x01\x02", "x"))

    def test_matches_against_the_realistic_wire_shape_with_trailer(self):
        # Not just a bare 0x40+frame: the real notification carries a 4-byte
        # unix timestamp and 2 filler bytes after the frame (see
        # realistic_notify()'s docstring). The matcher must still find the
        # token with that trailer present.
        frame, token = pbc.build_pong_probe("DK5EN-97", "DK5EN-93")
        self.assertTrue(pbc.frame_contains_token(realistic_notify(frame), token))

    def test_an_echoed_reject_command_does_not_false_positive(self):
        # A production image rejects --injectraw and echoes it back over BLE
        # through addBLECommandBack(): a real, parseable text frame (source
        # "response", destination "*") whose payload is the rejected command
        # line, i.e. "--wrong command --injectraw <probe frame as hex>". That
        # echo arrives for BOTH probes; if either token could occur in it, an
        # unfixed image would report PASS. The guard is that both tokens
        # carry non-hex characters.
        for builder in (pbc.build_control_probe, pbc.build_pong_probe):
            probe, token = builder("DK5EN-97", "DK5EN-93")
            echo = pbc.build_frame(
                0x01020304, 0x00, ["response"], b"*",
                b"--wrong command --injectraw " + probe.hex().encode("ascii") + b"\n")
            self.assertIsNotNone(mc_frame.parse(echo), "the echo must model a parseable frame")
            self.assertFalse(pbc.frame_contains_token(realistic_notify(echo), token))


# --------------------------------------------------------------- src-call guard


class TestCheckSrcCall(unittest.TestCase):
    def test_foreign_callsign_is_refused(self):
        msg = pbc.check_src_call("OE1XAR-62")
        self.assertIsNotNone(msg)
        self.assertIn("DK5EN-", msg)

    def test_dk5en_callsign_is_allowed(self):
        self.assertIsNone(pbc.check_src_call("DK5EN-97"))

    def test_case_insensitive(self):
        self.assertIsNone(pbc.check_src_call("dk5en-97"))

    def test_bare_dk5en_without_dash_is_refused(self):
        # "DK5EN" alone is not "DK5EN-" -- must still be refused, not treated
        # as a prefix match that happens to pass.
        self.assertIsNotNone(pbc.check_src_call("DK5EN2"))

    def test_main_exits_2_on_refusal(self):
        with self.assertRaises(SystemExit) as ctx:
            pbc.main(["--src-call", "OE1XAR-62", "--own-call", "DK5EN-93",
                      "--name", "DK5EN-93"])
        self.assertEqual(ctx.exception.code, 2)


# --------------------------------------------------------------- verdict


class TestVerdict(unittest.TestCase):
    def test_both_seen_is_pass(self):
        self.assertEqual(pbc.verdict(True, True), 0)

    def test_control_only_is_fail_fix_absent(self):
        self.assertEqual(pbc.verdict(True, False), 1)

    def test_control_missing_is_void_regardless_of_pong(self):
        self.assertEqual(pbc.verdict(False, False), 3)
        self.assertEqual(pbc.verdict(False, True), 3)

    def test_all_codes_have_a_message(self):
        for control_seen in (True, False):
            for pong_seen in (True, False):
                code = pbc.verdict(control_seen, pong_seen)
                self.assertIn(code, pbc.VERDICT_MESSAGES)


# --------------------------------------------------------------- self-test entry point


class TestSelfTest(unittest.TestCase):
    def test_self_test_passes(self):
        self.assertEqual(pbc._self_test(), 0)


if __name__ == "__main__":
    unittest.main()
