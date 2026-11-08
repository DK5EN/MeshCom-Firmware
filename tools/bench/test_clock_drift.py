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


class _RecSerial(_DripSerial):
    """_DripSerial that records writes; optionally raises on the Nth read after `fail_after` seconds."""

    def __init__(self, schedule: list[tuple[float, bytes]], fail_at: float | None = None) -> None:
        super().__init__(schedule)
        self.writes = bytearray()
        self.fail_at = fail_at
        self.closed = False

    def write(self, b: bytes) -> int:
        self.writes += b
        return len(b)

    def flush(self) -> None:
        pass

    def close(self) -> None:
        self.closed = True

    def read(self, n: int) -> bytes:
        if self.fail_at is not None and self.now >= self.fail_at:
            raise OSError("device disconnected")
        return super().read(n)

    def commands(self) -> list[str]:
        return [c for c in self.writes.decode().split("\n") if c]


READY = (0.5, b"[BOOT];ready;ms;4000;ip;0\n")


class LiveHardeningTest(unittest.TestCase):
    def setUp(self) -> None:
        import unittest.mock as mock
        p = mock.patch.object(cd.time, "sleep", lambda s: None)
        p.start()
        self.addCleanup(p.stop)
        self._td = tempfile.TemporaryDirectory()
        self.addCleanup(self._td.cleanup)
        self.tmp = Path(self._td.name)

    def sink(self) -> cd.NodeSink:
        return cd.NodeSink("n1", self.tmp / "clock-n1-test", jsonl=False)

    def test_startup_waits_for_boot_ready(self) -> None:
        import threading
        ser = _RecSerial([(0.1, b"rst:0x1 (POWERON_RESET)\n"), (8.0, b"[BOOT];ready;ms;4000;ip;0\n")])
        cd.run_live(self.sink(), ser, 12.0, ["--clockstat"], [], threading.Event(), clock=ser.clock)
        self.assertEqual(ser.commands(), ["--clockstat"])
        self.assertGreaterEqual(ser.now, 8.0)

    def test_silent_node_is_asked_after_three_seconds(self) -> None:
        import threading
        ser = _RecSerial([])
        cd.run_live(self.sink(), ser, 6.0, ["--clockstat"], [], threading.Event(), clock=ser.clock)
        self.assertEqual(ser.commands(), ["--clockstat"])

    def test_boot_wait_timeout_sends_nothing(self) -> None:
        import threading
        ser = _RecSerial([(0.1, b"rst:0x1 (POWERON_RESET)\n"), (0.2, b"[BOOT];warn\n")])
        with self.assertRaises(cd.BootWaitTimeout):
            cd.run_live(self.sink(), ser, 100.0, ["--timesrc cet"], [], threading.Event(),
                        clock=ser.clock, boot_wait_s=10.0)
        self.assertEqual(ser.commands(), [])

    def test_timesrc_restored_on_exception(self) -> None:
        import threading
        ser = _RecSerial([READY], fail_at=5.0)
        with self.assertRaises(OSError):
            cd.run_live(self.sink(), ser, 100.0, ["--timesrc cet"], [], threading.Event(), clock=ser.clock)
        self.assertEqual(ser.commands(), ["--timesrc cet", "--timesrc all"])

    def test_timesrc_restored_on_normal_end_and_not_when_all(self) -> None:
        import threading
        ser = _RecSerial([READY])
        cd.run_live(self.sink(), ser, 6.0, ["--timesrc none"], [], threading.Event(), clock=ser.clock)
        self.assertEqual(ser.commands(), ["--timesrc none", "--timesrc all"])
        ser2 = _RecSerial([READY])
        cd.run_live(self.sink(), ser2, 6.0, ["--timesrc all"], [], threading.Event(), clock=ser2.clock)
        self.assertEqual(ser2.commands(), ["--timesrc all"])

    def test_worker_closes_port_and_records_error(self) -> None:
        import threading
        ser = _RecSerial([READY], fail_at=5.0)
        errors: dict[str, str] = {}
        sink = self.sink()
        orig = cd.run_live
        cd.run_live = lambda *a, **k: orig(*a, clock=ser.clock, **k)  # type: ignore[assignment]
        try:
            cd.live_worker(sink, "/dev/fake", 100.0, ["--timesrc cet"], [], threading.Event(), errors,
                           opener=lambda dev: ser)
        finally:
            cd.run_live = orig  # type: ignore[assignment]
        self.assertTrue(ser.closed)
        self.assertIn("OSError", errors["n1"])
        self.assertEqual(ser.commands()[-1], "--timesrc all")

    def test_unknown_skip_reason_is_counted_not_malformed(self) -> None:
        sink = self.sink()
        cd.replay(sink, ["[DISPCLK];skip;newreason", "[DISPCLK];skip;reason;other", "[DISPCLK];skip;newreason"])
        summ = sink.close()
        self.assertEqual(summ["skip_reasons"], {"newreason": 2, "other": 1})
        self.assertEqual(summ["malformed_lines"], 0)
        self.assertNotIn("newreason", cd.SKIP_REASONS)

    def test_whitelist_unchanged(self) -> None:
        for bad in ("--reboot", "--timesrc bogus", "--clockstat x", "--info"):
            with self.assertRaises(ValueError):
                cd.allowed_command(bad)


class MainRefusalTest(unittest.TestCase):
    def setUp(self) -> None:
        import unittest.mock as mock
        self.mock = mock
        self._td = tempfile.TemporaryDirectory()
        self.addCleanup(self._td.cleanup)
        self.out = self._td.name
        fleet = {"nodes": {"n1": {}}}
        for name, val in (("load_fleet", fleet), ("list_comports", [object()]),
                          ("discover_ports", {"n1": "/dev/cu.fake"})):
            p = mock.patch.object(cd.bench_suite, name, return_value=val)
            p.start()
            self.addCleanup(p.stop)
        po = mock.patch.object(cd, "open_port", side_effect=AssertionError("a real port must never be opened"))
        po.start()
        self.addCleanup(po.stop)

    def run_main(self, *extra: str) -> int:
        return cd.main(["--node", "n1", "--duration", "1", "--out", self.out, *extra])

    def test_refused_without_yes_reboot(self) -> None:
        with self.mock.patch.object(cd, "check_identity") as g:
            self.assertEqual(self.run_main(), 2)
            g.assert_not_called()

    def test_refused_when_port_busy(self) -> None:
        with self.mock.patch.object(cd.bench_suite, "port_busy", return_value=True), \
                self.mock.patch.object(cd, "check_identity") as g:
            self.assertEqual(self.run_main("--yes-reboot"), 2)
            g.assert_not_called()

    def test_refused_when_identity_guard_fails(self) -> None:
        with self.mock.patch.object(cd.bench_suite, "port_busy", return_value=False), \
                self.mock.patch.object(cd, "check_identity", side_effect=RuntimeError("placeholder call")), \
                self.mock.patch.object(cd, "live_worker") as w:
            self.assertEqual(self.run_main("--yes-reboot"), 2)
            w.assert_not_called()

    def test_guard_pass_runs_worker(self) -> None:
        with self.mock.patch.object(cd.bench_suite, "port_busy", return_value=False), \
                self.mock.patch.object(cd, "check_identity") as g, \
                self.mock.patch.object(cd, "live_worker") as w:
            self.assertEqual(self.run_main("--yes-reboot"), 0)
            g.assert_called_once_with("n1", "/dev/cu.fake")
            w.assert_called_once()

    def test_offline_ok_passed_and_recorded_in_summary(self) -> None:
        with self.mock.patch.object(cd.bench_suite, "port_busy", return_value=False), \
                self.mock.patch.object(cd, "check_identity", return_value=["wifi", "web"]) as g, \
                self.mock.patch.object(cd, "live_worker") as w:
            self.assertEqual(self.run_main("--yes-reboot", "--offline-ok"), 0)
            g.assert_called_once_with("n1", "/dev/cu.fake", offline_ok=True)
            w.assert_called_once()
        summaries = list(Path(self.out).glob("clock-n1-*.summary"))
        self.assertEqual(len(summaries), 1)
        self.assertEqual(json.loads(summaries[0].read_text())["identity_waiver"], ["wifi", "web"])

    def test_check_identity_forwards_offline_ok(self) -> None:
        import identity_guard

        info = identity_guard.NodeInfo(None, None, None, None, None, None, None, waived=["clock"])
        with self.mock.patch.object(identity_guard, "require_node", return_value=info) as r:
            self.assertEqual(cd.check_identity("n1", "/dev/x", offline_ok=True), ["clock"])
            r.assert_called_once_with("n1", port="/dev/x", offline_ok=True)

    def test_summary_without_waiver_is_empty_list(self) -> None:
        with tempfile.TemporaryDirectory() as d:
            summ, _ = run([], Path(d))
        self.assertEqual(summ["identity_waiver"], [])
        self.assertEqual(cd.build_parser().parse_args(["--node", "n1"]).offline_ok, False)


if __name__ == "__main__":
    unittest.main()
