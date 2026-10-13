"""Tests for tools/bench/onrx_matrix.py (parser, statistics, arm mapping, restore, session).

Stdlib unittest only -- pytest is NOT available under the PlatformIO python that
tools/regression.sh uses. Run with:
    python3 -m unittest discover -s tools/mock -p test_onrx_matrix.py
"""

from __future__ import annotations

import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Callable

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "bench"))
import onrx_matrix as om  # noqa: E402


def line(seq: int, total: int, prn: int, nbytes: int, extra: str = "", irq: int = 900, ph: str = "a:1,b:2") -> str:
    return (f"2026-10-13 10:00:00.000  [INS] ONRX seq={seq} total_us={total} print_us={prn} "
            f"bytes={nbytes} len=40 irq_to_read_us={irq} ph={ph}{extra}")


class TestParserAndStats(unittest.TestCase):
    def test_parse_normal_unknown_malformed_lost(self) -> None:
        lines = [
            "[INS] ONRX_READY",
            line(1, 60000, 50000, 600),
            line(2, 40000, 30000, 400, " future=xyz"),
            "[INS] ONRX seq=3 print_us=5",  # no total_us -> malformed
            "[INS] ONRX seq=4 total_us=abc",  # bad int -> malformed
            line(5, 20000, 10000, 200, " lost=7"),
            "unrelated noise",
        ]
        p = om.parse_log(lines)
        self.assertIs(p["ready"], True)
        self.assertEqual(p["malformed"], 2)
        self.assertEqual([r["seq"] for r in p["onrx"]], [1, 2, 5])
        self.assertEqual(p["onrx"][0]["ph"], {"a": 1, "b": 2})
        r = om.analyse(lines, "A")
        self.assertEqual(r["n"], 3)
        self.assertEqual(r["lost_max"], 7)
        self.assertEqual(r["over_50ms_total"], 1)
        self.assertEqual(r["over_50ms_total_minus_print"], 0)
        self.assertEqual(r["total_us"]["p50"], 40000.0)
        self.assertEqual(r["total_us"]["max"], 60000.0)
        self.assertAlmostEqual(r["print_share"], 90000 / 120000)

    def test_exact_fit(self) -> None:
        # total = 87 * bytes + 5500, exactly linear
        lines = [line(i, 87 * b + 5500, 80 * b, b) for i, b in enumerate([100, 200, 300, 400, 500])]
        fit = om.analyse(lines)["fit_total_vs_bytes"]
        self.assertAlmostEqual(fit["slope"], 87.0, places=9)
        self.assertAlmostEqual(fit["intercept"], 5500.0, places=5)
        self.assertAlmostEqual(fit["r2"], 1.0, places=11)

    def test_fit_r2_known_value(self) -> None:
        f = om.linfit([1, 2, 3, 4], [2, 4, 5, 9])
        self.assertAlmostEqual(f["slope"], 2.2)
        self.assertAlmostEqual(f["intercept"], -0.5)
        self.assertAlmostEqual(f["r2"], 1 - 1.8 / 26.0)

    def test_percentile_interpolation(self) -> None:
        self.assertEqual(om.pct([1, 2, 3, 4, 5], 50), 3.0)
        self.assertEqual(om.pct([0, 10], 90), 9.0)
        self.assertIsNone(om.pct([], 50))

    def test_empty_and_short_logs(self) -> None:
        r = om.analyse([], "D")
        self.assertEqual(r["n"], 0)
        self.assertEqual(r["source"], "none")
        self.assertIsNone(r["total_us"]["p50"])
        self.assertIsNone(r["lost_max"])
        self.assertEqual(r["phases"], {})
        self.assertIsNone(r["txbuf"])
        self.assertIn("arm D", om.format_table(r))
        r1 = om.analyse([line(1, 1000, 0, 10)])
        self.assertEqual(r1["n"], 1)
        self.assertIsNone(r1["fit_total_vs_bytes"]["slope"])  # one point
        om.format_table(r1)

    def test_print_us_zero(self) -> None:
        r = om.analyse([line(1, 8000, 0, 100), line(2, 9000, 0, 200)])
        self.assertEqual(r["print_share"], 0.0)
        self.assertEqual(r["print_us"]["max"], 0.0)
        self.assertEqual(r["total_minus_print_us"]["max"], 9000.0)

    def test_fallback_onrxdone_time(self) -> None:
        r = om.analyse(["x [MC-DBG] ONRXDONE_TIME ms=56", "x [MC-DBG] ONRXDONE_TIME ms=60",
                        "[MC-WARN] ONRXDONE_SLOW ms=56"])
        self.assertEqual(r["source"], "ONRXDONE_TIME")
        self.assertEqual(r["n"], 2)
        self.assertEqual(r["total_us"]["p50"], 58000.0)
        self.assertNotIn("print_us", r)
        self.assertIn("no print_us", r["note"])
        self.assertIn("no print_us", om.format_table(r))

    def test_arm_commands(self) -> None:
        c = om.ARM_COMMANDS
        self.assertEqual(set(c), {"A", "B", "C", "D", "F"})
        self.assertEqual(c["A"], ["--loradebug on", "--setlog on", "--nbrdebug on"])
        self.assertEqual(c["F"], c["A"])
        self.assertEqual(set(c["B"]), {"--loradebug off", "--setlog on", "--nbrdebug on"})
        self.assertEqual(set(c["C"]), {"--loradebug off", "--setlog on", "--nbrdebug off"})
        self.assertEqual(set(c["D"]), {"--loradebug off", "--setlog off", "--nbrdebug off"})
        for cmds in c.values():
            self.assertTrue(all(x.split()[0] in ("--loradebug", "--setlog", "--nbrdebug") for x in cmds))


class TestIrqTxbufPhases(unittest.TestCase):
    def test_irq_no_stamp_dropped_zero_kept(self) -> None:
        lines = [line(1, 1000, 0, 10, irq=-1), line(2, 1000, 0, 10, irq=-1), line(3, 1000, 0, 10, irq=0),
                 line(4, 1000, 0, 10, irq=100), line(5, 1000, 0, 10, irq=300)]
        r = om.analyse(lines)
        self.assertEqual(r["irq_n"], 3)
        self.assertEqual(r["irq_no_stamp"], 2)
        self.assertEqual(r["irq_to_read_us"]["p50"], 100.0)
        self.assertEqual(r["irq_to_read_us"]["max"], 300.0)
        self.assertIn("no stamp 2", om.format_table(r))

    def test_irq_all_no_stamp(self) -> None:
        r = om.analyse([line(1, 1000, 0, 10, irq=-1)])
        self.assertEqual((r["irq_n"], r["irq_no_stamp"]), (0, 1))
        self.assertIsNone(r["irq_to_read_us"]["p50"])
        om.format_table(r)

    def test_txbuf_parsed_distinct_and_absent_tolerated(self) -> None:
        lines = [line(1, 1000, 0, 10, " lost=0 txbuf=4096/1"), line(2, 1000, 0, 10, " txbuf=4096/1"),
                 line(3, 1000, 0, 10, " txbuf=256/0"), line(4, 1000, 0, 10, " txbuf=junk")]
        r = om.analyse(lines)
        self.assertEqual(r["malformed"], 0)
        self.assertEqual(r["txbuf"], ["256/0", "4096/1"])
        self.assertTrue(r["txbuf_applied"])
        self.assertIn("txbuf 256/0,4096/1", om.format_table(r))
        old = om.analyse([line(1, 1000, 0, 10)])
        self.assertIsNone(old["txbuf"])
        self.assertFalse(old["txbuf_applied"])
        self.assertIn("txbuf -", om.format_table(old))

    def test_txbuf_not_applied(self) -> None:
        r = om.analyse([line(1, 1000, 0, 10, " txbuf=0/1"), line(2, 1000, 0, 10, " txbuf=4096/0")])
        self.assertFalse(r["txbuf_applied"])

    def test_phase_aggregation(self) -> None:
        lines = [line(1, 1000, 0, 10, ph="a:10,b:100"), line(2, 1000, 0, 10, ph="a:30"),
                 line(3, 1000, 0, 10, ph="a:20,b:300")]
        r = om.analyse(lines)
        self.assertEqual(list(r["phases"]), ["a", "b"])
        a, b = r["phases"]["a"], r["phases"]["b"]
        self.assertEqual((a["n"], a["p50"], a["max"]), (3, 20.0, 30.0))
        self.assertAlmostEqual(a["p99"], 29.8)
        self.assertEqual((b["n"], b["p50"], b["max"]), (2, 200.0, 300.0))
        self.assertAlmostEqual(b["p99"], 298.0)
        json.dumps(r)
        table = om.format_table(r)
        self.assertIn("ph a", table)
        self.assertIn("ph b", table)


class FakeConsole:
    """Console double. dies_in_capture: the long drain raises and every later call raises."""

    def __init__(self, dies_in_capture: bool = False, dead_on_send: bool = False,
                 capture_lines: tuple[str, ...] = ()) -> None:
        self.sent: list[str] = []
        self.dies = dies_in_capture
        self.dead = dead_on_send
        self.lines = capture_lines
        self.closed = False

    def send(self, cmd: str) -> None:
        if self.dead:
            raise BrokenPipeError("connection lost")
        self.sent.append(cmd)

    def drain(self, seconds: float, sink: Callable[[str], None]) -> None:
        if self.dead:
            raise BrokenPipeError("connection lost")
        if seconds > 100:
            for ln in self.lines:
                sink(ln)
            if self.dies:
                self.dead = True
                raise RuntimeError("console closed the connection")

    def close(self) -> None:
        self.closed = True


class Factory:
    """Hands out consoles in order; records how many connections were opened."""

    def __init__(self, *consoles: FakeConsole | Exception) -> None:
        self.queue = list(consoles)
        self.opened = 0

    def __call__(self) -> FakeConsole:
        self.opened += 1
        item = self.queue.pop(0)
        if isinstance(item, Exception):
            raise item
        return item


PRE: dict[str, bool | None] = {
    "loradebug": False, "nbrdebug": True, "displayinfo": True, "displaylog": False, "retx": False}
GOOD_LINES = ("[INS] ONRX_READY", line(1, 60000, 50000, 600, " txbuf=4096/1"),
              line(2, 4000, 100, 60, " txbuf=4096/1"))


class TestRestoreAndRunArm(unittest.TestCase):
    def test_restore_commands_order(self) -> None:
        self.assertEqual(om.restore_commands(PRE), [
            "--loradebug off", "--setinfo on", "--setretx off", "--setlog off", "--nbrdebug on"])
        self.assertEqual(om.restore_commands({"loradebug": None}), [])

    def test_parse_state(self) -> None:
        st = om.parse_state("...LORADEBUG on ...NBRDEBUG off ...DisplayInfo on ...DisplyLog off ...x")
        self.assertEqual(st, {"loradebug": True, "nbrdebug": False, "displayinfo": True,
                              "displaylog": False, "retx": True})
        self.assertIsNone(om.parse_state("...LORADEBUG on")["nbrdebug"])

    def test_clean_run(self) -> None:
        con = FakeConsole()
        self.assertEqual(om.run_arm(con, "D", PRE, 1.0, lambda s: None), (None, []))
        self.assertEqual(con.sent[-5:], om.restore_commands(PRE))

    def test_capture_error_without_connection_loss(self) -> None:
        con = FakeConsole()

        def drain(seconds: float, sink: Callable[[str], None]) -> None:
            if seconds > 100:
                raise RuntimeError("boom")

        con.drain = drain  # type: ignore[method-assign]
        err, errors = om.run_arm(con, "A", PRE, 600.0, lambda s: None)
        self.assertIn("boom", err or "")
        self.assertEqual(errors, [])
        self.assertEqual(con.sent[:3], om.ARM_COMMANDS["A"])
        self.assertEqual(con.sent[3:], om.restore_commands(PRE))

    def test_connection_loss_restored_via_fresh_connection(self) -> None:
        con = FakeConsole(dies_in_capture=True)
        second = FakeConsole()
        fac = Factory(second)
        err, errors = om.run_arm(con, "A", PRE, 600.0, lambda s: None, reconnect=fac)
        self.assertIn("console closed", err or "")
        self.assertEqual(errors, [])
        self.assertEqual(fac.opened, 1)
        self.assertEqual(second.sent, om.restore_commands(PRE))
        self.assertTrue(second.closed)

    def test_connection_loss_retry_also_fails(self) -> None:
        con = FakeConsole(dies_in_capture=True)
        fac = Factory(FakeConsole(dead_on_send=True))
        err, errors = om.run_arm(con, "A", PRE, 600.0, lambda s: None, reconnect=fac)
        self.assertIsNotNone(err)
        self.assertEqual(fac.opened, 1)  # exactly one fresh connection
        self.assertEqual(len(errors), len(om.restore_commands(PRE)))
        self.assertTrue(all("connection lost" in e for e in errors))

    def test_connection_loss_reconnect_refused(self) -> None:
        con = FakeConsole(dies_in_capture=True)
        fac = Factory(ConnectionRefusedError("no route"))
        _, errors = om.run_arm(con, "A", PRE, 600.0, lambda s: None, reconnect=fac)
        self.assertTrue(any("reconnect: no route" in e for e in errors))
        self.assertGreaterEqual(len(errors), len(om.restore_commands(PRE)) + 1)

    def test_keyboard_interrupt_restores_then_propagates(self) -> None:
        con = FakeConsole()

        def drain(seconds: float, sink: Callable[[str], None]) -> None:
            if seconds > 100:
                raise KeyboardInterrupt

        con.drain = drain  # type: ignore[method-assign]
        with self.assertRaises(KeyboardInterrupt):
            om.run_arm(con, "A", PRE, 600.0, lambda s: None)
        self.assertEqual(con.sent[3:], om.restore_commands(PRE))


class TestSession(unittest.TestCase):
    def session(self, fac: Factory, arm: str = "A",
                pre: dict[str, bool | None] | None = None) -> tuple[int, str]:
        with tempfile.TemporaryDirectory() as td:
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                rc = om.run_session(fac, arm, PRE if pre is None else pre, 10.0, Path(td))
            logs = sorted(Path(td).glob("*.log"))
            jsons = sorted(Path(td).glob("*.json"))
            self.assertEqual(len(logs), len(jsons))  # a report is written whenever a log exists
            if jsons:
                self.assertEqual(json.loads(jsons[0].read_text())["arm"], arm)
            return rc, buf.getvalue()

    def test_clean_session_rc0(self) -> None:
        con = FakeConsole(capture_lines=GOOD_LINES)
        rc, out = self.session(Factory(con))
        self.assertEqual(rc, 0, out)
        self.assertNotIn("RESTORE ERRORS", out)
        self.assertTrue(con.closed)

    def test_connection_loss_restore_succeeds_on_retry(self) -> None:
        first = FakeConsole(dies_in_capture=True, capture_lines=GOOD_LINES)
        second = FakeConsole()
        rc, out = self.session(Factory(first, second))
        self.assertEqual(rc, 4, out)  # capture error, restore clean
        self.assertEqual(second.sent, om.restore_commands(PRE))
        self.assertNotIn("RESTORE ERRORS", out)
        self.assertIn("CAPTURE ERROR", out)
        self.assertIn("n 2", out)  # partial log still analysed

    def test_connection_loss_retry_fails_rc2_and_report_written(self) -> None:
        first = FakeConsole(dies_in_capture=True, capture_lines=GOOD_LINES)
        rc, out = self.session(Factory(first, FakeConsole(dead_on_send=True)))
        self.assertEqual(rc, 2, out)
        self.assertIn("RESTORE ERRORS: ", out)
        self.assertIn("--loradebug off", out)
        self.assertIn("CAPTURE ERROR", out)
        self.assertIn("n 2", out)  # report on the partial log was still produced

    def test_arm_f_without_txbuf_is_loud_failure(self) -> None:
        con = FakeConsole(capture_lines=("[INS] ONRX_READY", line(1, 4000, 100, 60)))
        rc, out = self.session(Factory(con), arm="F")
        self.assertEqual(rc, 5, out)
        self.assertIn("WARNING: arm F", out)

    def test_arm_f_with_txbuf_ok(self) -> None:
        rc, out = self.session(Factory(FakeConsole(capture_lines=GOOD_LINES)), arm="F")
        self.assertEqual(rc, 0, out)

    def test_arm_a_without_txbuf_not_flagged(self) -> None:
        con = FakeConsole(capture_lines=("[INS] ONRX_READY", line(1, 4000, 100, 60)))
        rc, _ = self.session(Factory(con), arm="A")
        self.assertEqual(rc, 0)

    def test_unknown_pre_state_refuses_before_any_command(self) -> None:
        for key in ("loradebug", "nbrdebug", "displaylog"):
            pre = dict(PRE)
            pre[key] = None
            con = FakeConsole()
            fac = Factory(con)
            rc, out = self.session(fac, pre=pre)
            self.assertNotEqual(rc, 0, key)
            self.assertIn(key, out)
            self.assertEqual(fac.opened, 0, key)  # not even connected
            self.assertEqual(con.sent, [], key)

    def test_missing_pre_state_helper(self) -> None:
        self.assertEqual(om.missing_pre_state(PRE), [])
        self.assertEqual(om.missing_pre_state(om.parse_state("...LORADEBUG on")), ["nbrdebug", "displaylog"])


if __name__ == "__main__":
    unittest.main()
