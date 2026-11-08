#!/usr/bin/env python3
"""Unit tests for tools/bench/clock_drift.py (synthetic logs, no serial port, temp output dir)."""

from __future__ import annotations

import base64
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import clock_drift as cd  # noqa: E402


def stat(t: float, epoch: int, lost: int = 0) -> str:
    return (f"{t:.3f}  [CLOCK];stat;epoch;{epoch};ms;1000;ticks;60;late_sum_ms;5;late_max_ms;3;"
            f"lost_ms;{lost};src;NTP")


def run(lines: list[str], tmp: Path) -> tuple[dict, cd.NodeSink]:
    sink = cd.NodeSink("n1", tmp / "clock-n1-test")
    cd.replay(sink, lines)
    return sink.close(), sink


class ClockDriftTest(unittest.TestCase):
    def setUp(self) -> None:
        self._td = tempfile.TemporaryDirectory()
        self.tmp = Path(self._td.name)

    def tearDown(self) -> None:
        self._td.cleanup()

    def test_steady_drift(self) -> None:
        base = 1_800_000_000
        # Node gains 0.1 s per 60 s of host time.
        lines = [stat(base + 60 * i, base + 60 * i + (i * 0.1)) for i in range(11)]
        summ, _ = run(lines, self.tmp)
        self.assertEqual(summ["stat_count"], 11)
        self.assertAlmostEqual(summ["drift_total_s"], 1.0, places=2)
        self.assertAlmostEqual(summ["drift_ppm"], 1000000 / 600, delta=1.0)
        self.assertEqual(summ["drift_curve"][0]["drift_s"], 0)
        self.assertTrue((self.tmp / "clock-n1-test.summary").exists())
        recs = [json.loads(x) for x in (self.tmp / "clock-n1-test.jsonl").read_text().splitlines()]
        self.assertEqual(len(recs), 11)
        self.assertEqual(recs[0]["tag"], "CLOCK")

    def test_cet_set_jump(self) -> None:
        lines = [
            "1800000000.0  [CLOCK];set;src;CET;old;1799999940;new;1800000001;delta_ms;61000",
            "1800003600.0  [CLOCK];set;src;CET;old;1800003600;new;1800003601;delta_ms;-1500",
            "1800003601.0  [CLOCK];set;src;NTP;old;5;new;6",  # delta derived from old/new
        ]
        summ, _ = run(lines, self.tmp)
        self.assertEqual([s["delta_ms"] for s in summ["sets"]], [61000, -1500, 1000])
        self.assertEqual(summ["set_by_src"]["CET"], {"count": 2, "max_abs_delta_ms": 61000})
        self.assertEqual(summ["max_abs_set_delta_ms"], 61000)

    def test_display_gap_and_skips(self) -> None:
        t = 1800000000.0
        lines = [
            f"{t}  [DISPCLK];draw;hms;12:00:00;type;1",
            f"{t + 1}  [DISPCLK];draw;hms;12:00:01;type;1",
            f"{t + 2}  [DISPCLK];skip;gate15",
            f"{t + 3}  [DISPCLK];skip;gate15",
            f"{t + 4}  [DISPCLK];skip;reason;type0",
            f"{t + 5}  [DISPCLK];draw;hms;12:00:05;type;0",
            f"{t + 6}  [DISPCLK];draw;hms;12:00:06;type;0",
        ]
        summ, _ = run(lines, self.tmp)
        self.assertEqual(summ["draw_count"], 4)
        self.assertEqual(summ["display_gap_max_s"], 4.0)
        self.assertEqual(summ["display_gap_at"]["from_hms"], "12:00:01")
        self.assertEqual(summ["display_gap_at"]["to_hms"], "12:00:05")
        self.assertEqual(summ["display_gaps_over_threshold"], 1)
        self.assertEqual(summ["skip_reasons"], {"gate15": 2, "type0": 1})

    def test_space_separated_form_parses(self) -> None:
        ev = cd.parse_event("[CLOCK] set src CET old 10 new 12 delta_ms 2000")
        self.assertEqual(ev, ("CLOCK", "set", {"src": "CET", "old": 10.0, "new": 12.0, "delta_ms": 2000}))

    def test_malformed_ignored(self) -> None:
        lines = [
            "1800000000.0  boot noise without a tag",
            "1800000001.0  [CLOCK];stat;epoch;abc;ms;1",
            "1800000002.0  [CLOCK];set;src;CET;old;1",
            "1800000003.0  [DISPCLK];draw;hms;noon;type;1",
            "1800000004.0  [DISPCLK];skip",
            "1800000005.0  [OLEDSHOT];128x64;!!!notbase64",
            "1800000006.0  [CLOCK];bogus;x;1",
            "garbage",
            stat(1800000010.0, 1800000010),
        ]
        summ, _ = run(lines, self.tmp)
        self.assertEqual(summ["stat_count"], 1)
        self.assertEqual(summ["malformed_lines"], 5)
        self.assertEqual(summ["shot_errors"], 1)
        self.assertEqual(summ["draw_count"], 0)

    def test_info_lines_are_not_malformed(self) -> None:
        lines = [
            "1800000000.0  [CLOCK];timesrc;cet",
            "1800000001.0  [CLOCK];timesrc;err;usage --timesrc all|cet|none;now;cet",
            "1800000002.0  [OLEDSHOT];err;no display",
            "1800000003.0  [TDSHOT];err;shadow was off, now on - repeat --tdshot after the next redraw",
            "1800000004.0  [TDSHOT];err;T-Deck only",
            "1800000005.0  [CLOCK];timesrc;bogus",       # unknown mode stays malformed
            stat(1800000010.0, 1800000010),
        ]
        summ, _ = run(lines, self.tmp)
        self.assertEqual(summ["malformed_lines"], 1)
        self.assertEqual(summ["stat_count"], 1)
        self.assertEqual(summ["shot_errors"], 0)
        info = summ["info_events"]
        self.assertEqual([(i["tag"], i["kind"]) for i in info],
                         [("CLOCK", "timesrc"), ("CLOCK", "timesrc"), ("OLEDSHOT", "err"),
                          ("TDSHOT", "err"), ("TDSHOT", "err")])
        self.assertEqual(info[0]["mode"], "cet")
        self.assertEqual(info[1]["now"], "cet")
        self.assertEqual(info[2]["msg"], "no display")
        self.assertTrue(info[3]["msg"].startswith("shadow was off"))

    def test_info_parse_event(self) -> None:
        self.assertEqual(cd.parse_event("[CLOCK];timesrc;none"), ("CLOCK", "timesrc", {"mode": "none"}))
        self.assertEqual(cd.parse_event("[OLEDSHOT];err;display off"), ("OLEDSHOT", "err", {"msg": "display off"}))
        self.assertEqual(cd.parse_event("[TDSHOT];err;no memory"), ("TDSHOT", "err", {"msg": "no memory"}))

    def test_oled_roundtrip(self) -> None:
        w, h = 16, 8
        buf = bytearray(w * h // 8)
        buf[0] = 0x01       # pixel (0,0)
        buf[w - 1] = 0x80   # pixel (15,7)
        b64 = base64.b64encode(bytes(buf)).decode()
        gray = cd.oled_screenshot.unpack(bytes(buf), w, h)
        self.assertEqual((gray[0], gray[7 * w + 15], gray[1]), (255, 255, 0))
        summ, sink = run([f"1800000000.0  [OLEDSHOT];{w}x{h};{b64}"], self.tmp)
        self.assertEqual(summ["shots"], ["clock-n1-test-oled-001.png"])
        png = (self.tmp / summ["shots"][0]).read_bytes()
        self.assertTrue(png.startswith(b"\x89PNG\r\n\x1a\n"))
        self.assertEqual(int.from_bytes(png[16:20], "big"), w * cd.OLED_SCALE)
        self.assertEqual(int.from_bytes(png[20:24], "big"), h * cd.OLED_SCALE)

    def test_tdshot_framing(self) -> None:
        w, h = 4, 2
        raw = bytes([0xF8, 0x00] * (w * h))  # red
        half = len(raw) // 2
        enc = lambda b: base64.b64encode(b).decode()  # noqa: E731
        lines = [
            f"1800000000.0  [TDSHOT];begin;{w}x{h};{len(raw)};2",
            f"1800000000.1  [TDSHOT];chunk;1;{enc(raw[half:])}",
            "1800000000.2  [CLOCK];stat;epoch;1800000000;ms;1",   # interleaved line is fine
            f"1800000000.3  [TDSHOT];chunk;0;{enc(raw[:half])}",
            "1800000000.4  [TDSHOT];end",
            f"1800000001.0  [TDSHOT];begin;{w}x{h};{len(raw)};2",   # incomplete frame
            f"1800000001.1  [TDSHOT];chunk;0;{enc(raw[:half])}",
            "1800000001.2  [TDSHOT];end",
        ]
        summ, _ = run(lines, self.tmp)
        self.assertEqual(summ["shots"], ["clock-n1-test-tdeck-001.png"])
        self.assertEqual(summ["shot_errors"], 1)
        self.assertEqual(summ["stat_count"], 1)
        png = (self.tmp / summ["shots"][0]).read_bytes()
        self.assertEqual(int.from_bytes(png[16:20], "big"), w)

    def test_command_whitelist(self) -> None:
        for ok in ("--clockstat", "--oledshot", "--tdshot", "--timesrc cet", "--timesrc none", "--timesrc all"):
            self.assertEqual(cd.allowed_command(ok), ok)
        for bad in ("--timesrc bogus", "--sendmsg x", "--mesh on", "--clockstat; --sendmsg x", ""):
            with self.assertRaises(ValueError):
                cd.allowed_command(bad)

    def test_iso_stamp(self) -> None:
        t, text = cd.parse_stamp("2026-11-07 12:00:00.250  [DISPCLK];skip;off", 0.0)
        self.assertEqual(text, "[DISPCLK];skip;off")
        self.assertAlmostEqual(t % 1, 0.25, places=2)


class _DripSerial:
    """Fake pyserial port: bytes arrive at scheduled host times; read() blocks like pyserial."""

    def __init__(self, schedule: list[tuple[float, bytes]], timeout: float = 0.5) -> None:
        self.now = 0.0
        self.timeout = timeout
        self._sched = [(t, bytearray(b)) for t, b in schedule]

    def clock(self) -> float:
        return self.now

    def _arrived(self) -> int:
        return sum(len(b) for t, b in self._sched if t <= self.now)

    @property
    def in_waiting(self) -> int:
        return self._arrived()

    def read(self, n: int) -> bytes:
        deadline = self.now + self.timeout
        while self._arrived() < n:
            nxt = [t for t, b in self._sched if t > self.now and b]
            if not nxt or min(nxt) > deadline:
                self.now = deadline
                break
            self.now = min(nxt)
        out = bytearray()
        for t, b in self._sched:
            if t <= self.now and b and len(out) < n:
                take = b[: n - len(out)]
                out += take
                del b[: len(take)]
        return bytes(out)


class _StampSink:
    def __init__(self) -> None:
        self.stamps: list[tuple[float, str]] = []

    def line(self, t: float, text: str) -> None:
        self.stamps.append((t, text))


class RunLiveStampTest(unittest.TestCase):
    def test_each_line_gets_its_own_arrival_stamp(self) -> None:
        # Slow console: lines arrive 0.1 s apart. A chunked read(4096) with a 0.5 s
        # timeout would stamp all of them at the timeout, corrupting gap and drift.
        sched = [(0.1 * (i + 1), f"[DISPCLK];draw;hms;12:00:0{i};type;1\n".encode()) for i in range(4)]
        ser = _DripSerial(sched)
        sink = _StampSink()
        import threading
        cd.run_live(sink, ser, duration_s=1.0, startup=[], periodic=[], stop=threading.Event(), clock=ser.clock)
        self.assertEqual(len(sink.stamps), 4)
        for i, (t, _) in enumerate(sink.stamps):
            self.assertAlmostEqual(t, 0.1 * (i + 1), delta=0.02, msg=f"line {i} stamped {t:.3f}")


if __name__ == "__main__":
    unittest.main()
