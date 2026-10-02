#!/usr/bin/env python3
"""Unit tests for the two BLE golden-instrument fixes (bug a and bug b).

Host-side, hardware-free: `ble_golden.py` only imports `bleak` inside `run()`,
so importing the module here never touches BLE. Run with either

    python3 -m pytest tools/bench/test_ble_golden.py -q
    python3 -m unittest tools/bench/test_ble_golden.py -v
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import ble_golden as bg  # noqa: E402  (path insert must come first)


# --------------------------------------------------------------- (a) timing


class TestWriteTimestamp(unittest.TestCase):
    """`on_write` must record the timestamp it is GIVEN, not `now`.

    This is the regression test for the write-after-await bug: the caller
    (`run()`'s `send()`) is required to take `time.monotonic()` before the
    `await client.write_gatt_char(...)`, so a slow round trip must not appear
    as a slow write. Simulated here without any BLE: on_write() is called
    with an explicit `t` that is earlier than "now", the way `send()` now
    calls it after the await has already happened.
    """

    def test_on_write_uses_the_passed_timestamp_not_now(self):
        cap = bg.Capture(("DK5EN-90",))
        # The write "started" 0.3 s before this call -- as if a slow
        # response=True round trip separated t0 from the moment on_write()
        # actually runs.
        pre_await_t = cap.t0 + 0.111
        cap.on_write(b"\xa0--info", "text '--info'", t=pre_await_t)
        recorded_t, frame, label, excluded = cap.writes[-1]
        self.assertAlmostEqual(recorded_t, 0.111, places=6)
        self.assertEqual(frame, b"\xa0--info")
        self.assertEqual(label, "text '--info'")
        self.assertFalse(excluded)

    def test_on_write_defaults_to_now_when_t_omitted(self):
        cap = bg.Capture()
        cap.on_write(b"\xa0--info", "text '--info'")
        recorded_t, _frame, _label, excluded = cap.writes[-1]
        # "now" minus t0 (both taken moments apart in this test) must be a
        # small non-negative number, not some earlier/garbage value.
        self.assertGreaterEqual(recorded_t, 0.0)
        self.assertLess(recorded_t, 1.0)
        self.assertFalse(excluded)

    def test_reply_attributed_to_pre_await_time_not_post_await_time(self):
        """The scenario the bug actually broke: a fast reply arriving inside
        the true (pre-await) write-to-reply gap must still count as a reply,
        even though the write is only recorded after a slower round trip.
        """
        cap = bg.Capture(("DK5EN-90",))
        # Write "really" happened at t=0.000 (pre-await instant); the round
        # trip took 0.4s but on_write() is called with the correct t=0.0.
        cap.on_write(b"\xa0--info", "text '--info'", t=cap.t0 + 0.0)
        # A reply notification lands at t=0.35s -- within a 0.55s window of
        # the TRUE write time, but would be AFTER a post-await timestamp of
        # 0.4s only by luck; the important thing is it is not misattributed
        # to "no preceding write" or a wildly wrong gap.
        kind, label = cap.attribute(0.35, window=0.55)
        self.assertEqual(kind, "reply")
        self.assertEqual(label, "text '--info'")


# --------------------------------------------------------------- (b) restore


class TestMaxhopRestore(unittest.TestCase):
    """The corpus's `RESTORE:` convention and the parsed default value."""

    def test_maxhop_query_reply_parses_to_an_int(self):
        """What the node prints for a bare `--maxhop` query
        (command_functions.cpp:3875): `[MAXHOP];text;%d;pos;%d`. Only reaches
        Serial, never BLE (see writes.txt), which is why the corpus restores
        to the compile default instead of a value read back from the node --
        this test only pins down that the format, if it ever were readable
        here, parses the way the restore command is built.
        """
        sample = "[MAXHOP];text;4;pos;6\n"
        fields = sample.strip().split(";")
        self.assertEqual(fields[0], "[MAXHOP]")
        current = int(fields[2])
        self.assertEqual(current, 4)
        restore_cmd = f"--maxhop {current}"
        self.assertEqual(restore_cmd, "--maxhop 4")

    def test_read_corpus_flags_restore_line_as_excluded(self):
        corpus_dir = Path(__file__).resolve().parents[2] / "test" / "golden" / "corpus" / "ble"
        entries = bg.read_corpus(corpus_dir / "writes.txt")
        self.assertIn(("--maxhop 4", True), entries)
        # Every non-restore line must come back unflagged.
        self.assertIn(("--info", False), entries)
        # The restore line is not itself a "--maxhop 5"-shaped mutation left
        # unflagged -- it must be the LAST entry, restoring what came before.
        self.assertEqual(entries[-1], ("--maxhop 4", True))

    def test_read_corpus_roundtrip_on_a_temp_file(self):
        import tempfile
        text = (
            "# comment\n"
            "\n"
            "--info\n"
            "--maxhop 5\n"
            "RESTORE:--maxhop 4\n"
        )
        with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
            f.write(text)
            path = Path(f.name)
        try:
            entries = bg.read_corpus(path)
            self.assertEqual(
                entries,
                [("--info", False), ("--maxhop 5", False), ("--maxhop 4", True)],
            )
        finally:
            path.unlink()

    def test_restore_write_reply_is_excluded_from_the_compared_signature(self):
        """A reply attributed to an `excluded` write must be classified
        `restore`, not `reply` -- so `write_files()` keeps it out of the
        compared `.bin` the way a spontaneous or mesh frame is kept out.
        """
        cap = bg.Capture(("DK5EN-90",))
        cap.on_write(b"\xa0--maxhop 4", "restore '--maxhop 4'",
                     t=cap.t0 + 1.0, excluded=True)
        kind, label = cap.attribute(1.2, window=0.55)
        self.assertEqual(kind, "restore")
        self.assertEqual(label, "restore '--maxhop 4'")

    def test_non_restore_write_reply_is_still_a_plain_reply(self):
        cap = bg.Capture(("DK5EN-90",))
        cap.on_write(b"\xa0--info", "text '--info'", t=cap.t0 + 1.0,
                     excluded=False)
        kind, label = cap.attribute(1.2, window=0.55)
        self.assertEqual(kind, "reply")
        self.assertEqual(label, "text '--info'")


if __name__ == "__main__":
    unittest.main()


class RestoreOverrideTest(unittest.TestCase):
    """--restore "<cmd> <value>" replaces the value of the matching RESTORE: line."""

    def test_override_replaces_corpus_default(self):
        import tempfile
        from pathlib import Path
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "writes.txt"
            p.write_text("--info\nRESTORE:--maxhop 4\n")
            got = bg.read_corpus(p, bg.parse_restore_args(["--maxhop 2"]))
            self.assertEqual(got, [("--info", False), ("--maxhop 2", True)])
            # a different command is untouched
            got = bg.read_corpus(p, bg.parse_restore_args(["--txpower 1"]))
            self.assertEqual(got[1], ("--maxhop 4", True))

    def test_bare_word_is_rejected(self):
        with self.assertRaises(SystemExit):
            bg.parse_restore_args(["--maxhop"])
