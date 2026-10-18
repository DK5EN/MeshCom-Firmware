#!/usr/bin/env python3
"""Unit tests for tools/bench/heap_idle.py (fixtures only, no hardware)."""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from unittest import mock
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import heap_idle as hi  # noqa: E402

BOOT = """\
rst:0x1 (POWERON_RESET)
MeshCom 4.40a.10.18 booting
[HEAP];00:00:03;182340;180000;110580;(init)
[PSRM];00:00:03;0
[HEAP];00:00:09;170100;168000;100000;(ble)
[HEAP];00:01:00;175000;165000;98304;(mon)
[HEAP];00:05:00;174000;164000;98304;(mon)
""".splitlines()


# printfdeb() default form: every ';' became a space (src/printfdeb_format.h).
BOOT_SPACE = """\
rst:0x1 (POWERON_RESET)
MeshCom 4.40a.10.18 booting
[HEAP] 00:00:03 182340 180000 110580 (init)
[PSRM] 00:00:03 0
[HEAP] 00:00:09 170100 168000 100000 (ble)
[HEAP] 00:01:00 175000 165000 98304 (mon)
[PSRM] 00:01:00 0 (mon)
[HEAP] 2026-10-18 14:02:11 174000 164000 98304 (mon)
[PSRM] 2026-10-18 14:02:11 0 (mon)
""".splitlines()


def report_of(lines: list[str]) -> hi.HeapReport:
    r = hi.HeapReport()
    for ln in lines:
        r.feed(ln)
    return r


class TestParse(unittest.TestCase):
    def test_figures(self) -> None:
        f = report_of(BOOT).figures()
        self.assertEqual(f["init_free"], 182340)
        self.assertEqual(f["last_mon_free"], 174000)
        self.assertEqual(f["idle_free"], 174000)
        self.assertEqual(f["min_free"], 164000)
        self.assertEqual(f["largest_block"], 110580)
        self.assertEqual(f["psram_last_free"], 0)
        self.assertEqual(f["firmware_version"], "4.40a.10.18")

    def test_boot_mark_labels(self) -> None:
        f = report_of(BOOT).figures()
        self.assertEqual(f["boot_marks"], [{"label": "ble", "free": 170100}])

    def test_noise_and_cr(self) -> None:
        r = report_of(["garbage", "[HEAP];1;100;90;80;(init)\r".rstrip("\r")])
        self.assertEqual(len(r.samples), 1)

    def test_only_init_uses_init_as_idle(self) -> None:
        f = report_of(BOOT[:3]).figures()
        self.assertIsNone(f["last_mon_free"])
        self.assertEqual(f["idle_free"], 182340)

    def test_no_heap_line(self) -> None:
        with self.assertRaises(hi.HeapError) as cm:
            report_of(["boot", "nothing"]).figures()
        self.assertIn("no [HEAP] line", str(cm.exception))


class TestSpaceForm(unittest.TestCase):
    def test_figures_space_form(self) -> None:
        f = report_of(BOOT_SPACE).figures()
        self.assertEqual(f["init_free"], 182340)
        self.assertEqual(f["last_mon_free"], 174000)
        self.assertEqual(f["min_free"], 164000)
        self.assertEqual(f["largest_block"], 110580)
        self.assertEqual(f["psram_last_free"], 0)
        self.assertEqual(f["boot_marks"], [{"label": "ble", "free": 170100}])
        self.assertEqual(f["samples"], 4)

    def test_time_with_space(self) -> None:
        r = report_of(["[HEAP] 2026-10-18 14:02:11 174000 164000 98304 (mon)"])
        self.assertEqual(r.samples[0].time, "2026-10-18 14:02:11")
        self.assertEqual(r.samples[0].free, 174000)

    def test_psram_time_with_space_unlabelled(self) -> None:
        r = report_of(["[PSRM] 2026-10-18 14:02:11 4096"])
        self.assertEqual(r.psram, [("2026-10-18 14:02:11", 4096, "")])

    def test_psram_space_form(self) -> None:
        r = report_of(["[PSRM] 00:00:03 8192", "[PSRM] 00:01:00 8000 (mon)"])
        self.assertEqual([p[1] for p in r.psram], [8192, 8000])

    def test_other_heap_tags_ignored(self) -> None:
        r = report_of(["12:00:00;[HEAP]Cnew;1234;(free)", "[HEAP]Cdis 12 (free)"])
        self.assertEqual(r.samples, [])


STAT_ONLY = """\
MeshCom booting
15:32:27 [LOG] STAT util=9 rx=26000 tx=1600 mh=3 heap=70900 trk=240/2 fw=40a/20261016 up=0 t=1
15:37:27 [LOG] STAT util=9 rx=26834 tx=1622 mh=3 heap=70484 trk=240/2 fw=40a/20261016 up=300 t=300006 onrx_n=29
""".splitlines()


class TestStat(unittest.TestCase):
    def test_stat_only_capture(self) -> None:
        r = report_of(STAT_ONLY)
        f = r.figures()
        self.assertEqual([s.label for s in r.samples], ["stat", "stat"])
        self.assertEqual(f["idle_free"], 70484)
        self.assertEqual(f["last_stat_free"], 70484)
        self.assertEqual(f["min_free_seen"], 70484)
        self.assertIsNone(f["last_mon_free"])
        self.assertIsNone(f["min_free"])
        self.assertIsNone(f["largest_block"])
        self.assertEqual(f["firmware_version"], "40a/20261016")
        self.assertEqual(r.samples[1].time, "15:37:27")

    def test_idle_is_later_of_mon_and_stat(self) -> None:
        later_stat = report_of(BOOT_SPACE[:6] + STAT_ONLY[1:]).figures()
        self.assertEqual(later_stat["idle_free"], 70484)
        later_mon = report_of(STAT_ONLY[1:] + BOOT_SPACE[:6]).figures()
        self.assertEqual(later_mon["idle_free"], 175000)

    def test_stat_without_heap_is_not_a_sample(self) -> None:
        r = report_of(["15:37:27 [LOG] STAT util=9 rx=1"])
        self.assertEqual(r.samples, [])

    def test_stat_line_starts_idle_timer(self) -> None:
        clock = [0.0]
        chunks = iter([STAT_ONLY[2].encode() + b"\n"])

        def read(_n: int) -> bytes:
            clock[0] += 1.0
            return next(chunks, b"")

        with mock.patch.object(hi.time, "monotonic", lambda: clock[0]):
            lines = list(hi._read_lines(read, 1000, 20, 5))
        self.assertEqual(lines, [STAT_ONLY[2]])
        self.assertLess(clock[0], 30)


class TestBootTimeout(unittest.TestCase):
    def _reader(self, clock: list[float], chunks: list[bytes], step: float):
        it = iter(chunks)

        def read(_n: int) -> bytes:
            clock[0] += step
            return next(it, b"")

        return read

    def test_no_heap_line_aborts(self) -> None:
        clock = [0.0]
        read = self._reader(clock, [b"boot noise\n"], 1.0)
        with mock.patch.object(hi.time, "monotonic", lambda: clock[0]):
            with self.assertRaises(hi.HeapError) as cm:
                list(hi._read_lines(read, 1000, 600, 5))
        self.assertIn("--boot-timeout-s", str(cm.exception))
        self.assertLess(clock[0], 10)

    def test_heap_line_in_time_does_not_abort(self) -> None:
        clock = [0.0]
        read = self._reader(clock, [b"[HEAP] 00:00:03 1 2 3 (init)\n"], 1.0)
        with mock.patch.object(hi.time, "monotonic", lambda: clock[0]):
            lines = list(hi._read_lines(read, 1000, 20, 5))
        self.assertEqual(lines, ["[HEAP] 00:00:03 1 2 3 (init)"])

    def test_main_exit_2(self) -> None:
        def fake(_port, _deadline, _idle, boot_timeout):
            yield "noise"
            raise hi.HeapError(f"no [HEAP] line within --boot-timeout-s {boot_timeout:g}")

        with tempfile.TemporaryDirectory() as d, mock.patch.object(
            hi, "serial_lines", fake
        ), mock.patch.object(hi, "HARNESS_DIR", Path(d)):
            rc = hi.main(["--port", "/dev/null", "--boot-timeout-s", "7", "--idle-s", "1"])
        self.assertEqual(rc, 2)


class TestBaseline(unittest.TestCase):
    def test_within_tolerance_passes(self) -> None:
        self.assertEqual(hi.compare_baseline(97000, 100000, 3072), (True, -3000))

    def test_exact_tolerance_passes(self) -> None:
        self.assertTrue(hi.compare_baseline(96928, 100000, 3072)[0])

    def test_below_tolerance_fails(self) -> None:
        self.assertEqual(hi.compare_baseline(96927, 100000, 3072), (False, -3073))

    def test_higher_passes(self) -> None:
        self.assertTrue(hi.compare_baseline(120000, 100000, 0)[0])

    def test_update_and_load(self) -> None:
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "b.json"
            hi.update_baseline(p, "ttgo_tbeam", report_of(BOOT).figures(), "usb")
            data = json.loads(p.read_text())
            e = data["ttgo_tbeam"]
            self.assertEqual(e["idle_free"], 174000)
            self.assertEqual(e["firmware_version"], "4.40a.10.18")
            self.assertEqual(e["channel"], "usb")
            self.assertIn("date", e)

    def test_run_capture_logs_raw(self) -> None:
        with tempfile.TemporaryDirectory() as d:
            log = Path(d) / "raw.log"
            r = hi.run_capture(iter(BOOT), log)
            self.assertEqual(len(r.samples), 4)
            self.assertEqual(log.read_text().splitlines(), BOOT)


if __name__ == "__main__":
    unittest.main()
