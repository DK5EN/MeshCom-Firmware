#!/usr/bin/env python3
"""Regression test: meshlogger captures name their time zone.

Before this change the line prefix was the host's local wall clock with no
zone anywhere in the capture, so a log copied off the rpizero could not be
put on UTC without guessing (2026-11-01: "rpizero runs CET" was a guess).

Now every log file opens with a ``[LOGGER] tz=...`` header, a DST jump is
marked in the file, ``--tz utc`` stamps UTC, and the line prefix keeps the
byte format every parser anchors on (nbrlog, berglog, soakstatus,
serial_monitor: ``^YYYY-MM-DD HH:MM:SS.mmm  rest``).

fails-before: Sink() takes no tz/clock and writes no header.

    python3 tools/bench/test_meshlogger_tz.py
"""

import os
import re
import sys
import tempfile
import unittest
from datetime import datetime, timedelta, timezone
from zoneinfo import ZoneInfo

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO, "tools"))
import meshlogger  # noqa: E402

# The prefix regex the harvesters use (tools/nbrlog.py:63, berglog.py:214).
RE_LINE = re.compile(r"^(?P<host>\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\s\s(?P<rest>.*)$")


class Clock:
    def __init__(self, start):
        self.now = start

    def __call__(self):
        return self.now


def read_all(outdir):
    text = ""
    for name in sorted(os.listdir(outdir)):
        if name.endswith(".log"):
            with open(os.path.join(outdir, name)) as fh:
                text += fh.read()
    return text


class TimezoneAwareCapture(unittest.TestCase):
    def test_header_names_zone_and_offset(self):
        berlin = ZoneInfo("Europe/Berlin")
        clock = Clock(datetime(2026, 11, 2, 10, 15, 3, 412000, tzinfo=berlin))
        with tempfile.TemporaryDirectory() as outdir:
            sink = meshlogger.Sink(outdir, tz=berlin, clock=clock)
            sink.write("RX_FRAME len=40")
            sink.close()
            lines = read_all(outdir).splitlines()
        self.assertEqual(len(lines), 2, lines)
        self.assertIn("[LOGGER] tz=Europe/Berlin utc_offset=+01:00", lines[0])
        self.assertIn("utc=2026-11-02T09:15:03.412Z", lines[0])
        self.assertEqual(lines[1], "2026-11-02 10:15:03.412  RX_FRAME len=40")
        for line in lines:
            self.assertRegex(line, RE_LINE)

    def test_utc_mode_stamps_utc(self):
        clock = Clock(datetime(2026, 11, 2, 9, 15, 3, 412000, tzinfo=timezone.utc))
        with tempfile.TemporaryDirectory() as outdir:
            sink = meshlogger.Sink(outdir, tz=meshlogger.resolve_tz("utc"), clock=clock)
            sink.write("x")
            sink.close()
            names = os.listdir(outdir)
            lines = read_all(outdir).splitlines()
        self.assertEqual(names, ["2026-11-02.log"])
        self.assertIn("tz=UTC utc_offset=+00:00", lines[0])
        self.assertEqual(lines[1], "2026-11-02 09:15:03.412  x")

    def test_dst_change_is_marked(self):
        berlin = ZoneInfo("Europe/Berlin")
        # 2027-03-28 01:59:59 CET -> 03:00:00 CEST
        before = datetime(2027, 3, 28, 0, 59, 59, tzinfo=timezone.utc).astimezone(berlin)
        clock = Clock(before)
        with tempfile.TemporaryDirectory() as outdir:
            sink = meshlogger.Sink(outdir, tz=berlin, clock=clock)
            sink.write("a")
            clock.now = (before.astimezone(timezone.utc) + timedelta(seconds=2)).astimezone(berlin)
            sink.write("b")
            sink.close()
            lines = read_all(outdir).splitlines()
        self.assertEqual(lines[1], "2027-03-28 01:59:59.000  a")
        self.assertIn("[LOGGER] tz=Europe/Berlin utc_offset changed +01:00 -> +02:00", lines[2])
        self.assertEqual(lines[3], "2027-03-28 03:00:01.000  b")

    def test_new_day_file_gets_its_own_header(self):
        utc = timezone.utc
        clock = Clock(datetime(2026, 11, 2, 23, 59, 59, tzinfo=utc))
        with tempfile.TemporaryDirectory() as outdir:
            sink = meshlogger.Sink(outdir, tz=utc, clock=clock)
            sink.write("a")
            clock.now = datetime(2026, 11, 3, 0, 0, 1, tzinfo=utc)
            sink.write("b")
            sink.close()
            with open(os.path.join(outdir, "2026-11-03.log")) as fh:
                second = fh.read().splitlines()
        self.assertIn("[LOGGER] tz=UTC", second[0])
        self.assertEqual(second[1], "2026-11-03 00:00:01.000  b")

    def test_resolve_tz(self):
        self.assertIsNone(meshlogger.resolve_tz("local"))
        self.assertIs(meshlogger.resolve_tz("UTC"), timezone.utc)
        self.assertEqual(str(meshlogger.resolve_tz("Europe/Vienna")), "Europe/Vienna")


if __name__ == "__main__":
    unittest.main(verbosity=2)
