#!/usr/bin/env python3
"""Regression tests for srvprobe.py (TM-39).

    python3 -m unittest tools/bench/experiments/test_srvprobe.py

All against synthetic transcripts through SrvReducer -- no hardware, no
serial port. SrvReducer never imports pyserial itself.
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import srvprobe  # noqa: E402


def feed(reducer, lines):
    for line in lines:
        reducer.add(line)
    return reducer


class TestSrvReducerBasics(unittest.TestCase):
    def test_srv_line_parsed(self):
        r = feed(srvprobe.SrvReducer("OE"), [
            "[GW];srv;OE;host;meshcom.oevsv.at;path;inet;ms;5123",
        ])
        self.assertEqual(r.srv, {"country": "OE", "host": "meshcom.oevsv.at",
                                  "path": "inet", "ms": 5123})

    def test_keep_and_reply_counted(self):
        r = feed(srvprobe.SrvReducer("OE"), [
            "[GW];keep;tx;ok;1;ms;1000",
            "[GW];rx;type;BEAT;len;22;ms;1050",
            "[GW];keep;tx;ok;1;ms;31000",
            "[GW];rx;type;BEAT;len;22;ms;31040",
        ])
        self.assertEqual(len(r.keep_tx), 2)
        self.assertEqual(r.gw_rx_by_type, {"BEAT": 2})
        self.assertEqual(r.total_rx(), 2)
        self.assertFalse(r.silent())

    def test_reset_counted_not_confused_with_other_lines(self):
        r = feed(srvprobe.SrvReducer("OE"), [
            "rst:0x1 (POWERON),boot:0x13 (SPI_FAST_FLASH_BOOT)",
            "[BOOT];ready;ms;4200;ip;1",
        ])
        self.assertEqual(r.resets, 1)
        self.assertEqual(r.ready, [(4200, 1)])

    def test_gw_rx_types_all_five(self):
        lines = [f"[GW];rx;type;{t};len;10;ms;{i}" for i, t in
                 enumerate(["SET", "CET", "BEAT", "DATA", "OTHER"], start=1)]
        r = feed(srvprobe.SrvReducer("DL"), lines)
        self.assertEqual(sorted(r.gw_rx_by_type), ["BEAT", "CET", "DATA", "OTHER", "SET"])
        self.assertEqual(r.total_rx(), 5)

    def test_udp_rx_tx_and_log_echo(self):
        r = feed(srvprobe.SrvReducer("IT"), [
            "[UDP];log;1",
            "[UDP];rx;ip;5.2.68.68;port;12061;len;22;head;4245415400",
            "[UDP];tx;ip;5.2.68.68;port;12061;len;30;ok;1",
            "[UDP];log;0",
        ])
        self.assertEqual(r.udplog_echo, [1, 0])
        self.assertEqual(len(r.udp_rx), 1)
        self.assertEqual(r.udp_rx[0], ("5.2.68.68", 12061, 22))
        self.assertEqual(len(r.udp_tx), 1)

    def test_dns_line(self):
        r = feed(srvprobe.SrvReducer("IT"), [
            "[WIFI];dns;meshcom.dig-italia.it;ip;5.2.68.68;ms;57",
        ])
        self.assertEqual(r.dns, [("meshcom.dig-italia.it", "5.2.68.68", 57)])

    def test_ntp_counted_by_kind(self):
        r = feed(srvprobe.SrvReducer("OE"), [
            "[NTP];ok;epoch;1788084488;rtt;106",
            "[NTP];timeout;ip;162.159.200.1;fails;1",
            "[NTP];ok;epoch;1788088088;rtt;98",
        ])
        self.assertEqual(r.ntp, {"ok": 2, "timeout": 1})

    def test_hb_warn_stage1_and_stage2_detected(self):
        # esp32_main.cpp's exact text (em dash included) -- the reducer only
        # substring-matches the two English phrases, not the dash.
        r = feed(srvprobe.SrvReducer("DL"), [
            "[UDP] Server not responding for 36s — WiFi CONNECTED",
            "[UDP] Heartbeat timeout 66s — WiFi CONNECTED, server unresponsive, waiting",
        ])
        self.assertEqual(len(r.hb_warn_lines), 2)

    def test_set_cet_content_line_opportunistic(self):
        r = feed(srvprobe.SrvReducer("OE"), [
            "RX-UDP Source-Path:OE1KBC-1,WIDE1-1",
            'aprsmsg payload preview: {SET}shortname long text here',
        ])
        self.assertEqual(len(r.set_cet_lines), 1)


class TestSilenceDetection(unittest.TestCase):
    def test_silent_when_keep_sent_and_nothing_returns(self):
        r = feed(srvprobe.SrvReducer("DL"), [
            "[GW];keep;tx;ok;1;ms;1000",
            "[GW];keep;tx;ok;1;ms;31000",
            "[GW];keep;tx;ok;1;ms;61000",
        ])
        self.assertTrue(r.silent())
        self.assertIsNone(r.first_answer_gap())

    def test_not_silent_when_udp_rx_arrives_even_without_gw_rx(self):
        r = feed(srvprobe.SrvReducer("DL"), [
            "[GW];keep;tx;ok;1;ms;1000",
            "[UDP];rx;ip;44.143.8.143;port;12061;len;22;head;4245415400",
        ])
        self.assertFalse(r.silent())

    def test_no_keep_sent_is_not_reported_silent(self):
        # nothing sent yet -- silence only means something after we tried
        r = srvprobe.SrvReducer("OE")
        self.assertFalse(r.silent())


class TestFirstAnswerGap(unittest.TestCase):
    def test_gap_is_none_before_first_keep(self):
        r = feed(srvprobe.SrvReducer("OE"), [
            "[GW];rx;type;BEAT;len;22;ms;500",  # before any KEEP -- ignored for the gap
        ])
        self.assertIsNone(r.first_answer_gap())

    def test_gap_counts_lines_between_first_keep_and_first_answer(self):
        r = feed(srvprobe.SrvReducer("OE"), [
            "[GW];keep;tx;ok;1;ms;1000",   # line 1
            "some unrelated line",          # line 2
            "some other unrelated line",    # line 3
            "[GW];rx;type;BEAT;len;22;ms;1050",  # line 4
        ])
        self.assertEqual(r.first_answer_gap(), 3)

    def test_only_first_keep_and_first_answer_matter(self):
        r = feed(srvprobe.SrvReducer("OE"), [
            "[GW];keep;tx;ok;1;ms;1000",
            "[GW];rx;type;BEAT;len;22;ms;1050",
            "[GW];keep;tx;ok;1;ms;31000",
            "[GW];rx;type;BEAT;len;22;ms;31050",
        ])
        self.assertEqual(r.first_answer_gap(), 1)


class TestSummaryRow(unittest.TestCase):
    def test_row_shape(self):
        r = feed(srvprobe.SrvReducer("OE"), [
            "[GW];srv;OE;host;meshcom.oevsv.at;path;inet;ms;5000",
            "[GW];keep;tx;ok;1;ms;5100",
            "[GW];rx;type;BEAT;len;22;ms;5150",
        ])
        row = r.summary_row()
        self.assertEqual(row["country"], "OE")
        self.assertEqual(row["host"], "meshcom.oevsv.at")
        self.assertEqual(row["path"], "inet")
        self.assertEqual(row["keep_tx"], 1)
        self.assertEqual(row["gw_rx_total"], 1)
        self.assertFalse(row["silent"])

    def test_row_shape_with_no_srv_line_seen(self):
        row = srvprobe.SrvReducer("IT").summary_row()
        self.assertEqual(row["host"], "?")
        self.assertEqual(row["path"], "?")


class TestVerdicts(unittest.TestCase):
    def _row(self, country, host, path="inet", keep=6, gw_rx=6, udp_rx=6,
             silent=False, hb_warn=0, ntp=None, resets=0, gap=1):
        return {
            "country": country, "host": host, "path": path, "dns_ip": "",
            "keep_tx": keep, "gw_rx_total": gw_rx, "gw_rx_by_type": {"BEAT": gw_rx},
            "udp_rx": udp_rx, "ntp": ntp or {}, "first_answer_gap": gap,
            "silent": silent, "hb_warn": hb_warn, "resets": resets,
        }

    def test_dl_oe_same_host_flagged(self):
        rows = [
            self._row("OE", "meshcom.oevsv.at"),
            self._row("DL", "meshcom.oevsv.at"),
            self._row("IT", "meshcom.dig-italia.it"),
        ]
        verdicts = srvprobe.render_verdicts(rows)
        joined = "\n".join(verdicts)
        self.assertIn("same host", joined)
        self.assertIn("meshcom.oevsv.at", joined)

    def test_dl_oe_different_host_not_flagged(self):
        rows = [
            self._row("OE", "44.143.8.143", path="hamnet"),
            self._row("DL", "meshcom.hamnet.cloud", path="hamnet"),
            self._row("IT", "meshcom.dig-italia.it"),
        ]
        verdicts = srvprobe.render_verdicts(rows)
        self.assertFalse(any("same host" in v for v in verdicts))

    def test_silent_country_flagged(self):
        rows = [
            self._row("OE", "meshcom.oevsv.at"),
            self._row("DL", "meshcom.oevsv.at", gw_rx=0, udp_rx=0, silent=True, gap=None, keep=6),
            self._row("IT", "meshcom.dig-italia.it"),
        ]
        verdicts = srvprobe.render_verdicts(rows)
        self.assertTrue(any("DL" in v and "silent" in v for v in verdicts))

    def test_hb_warn_flagged(self):
        rows = [
            self._row("OE", "meshcom.oevsv.at", hb_warn=2),
        ]
        verdicts = srvprobe.render_verdicts(rows)
        self.assertTrue(any("heartbeat-timeout" in v for v in verdicts))

    def test_no_differences_falls_back_to_explicit_line(self):
        # A single country has nothing to compare against -- no host-parity
        # check, no reply-count check, no silence/hb_warn triggered.
        rows = [self._row("OE", "meshcom.oevsv.at", gw_rx=4, keep=4)]
        verdicts = srvprobe.render_verdicts(rows)
        self.assertEqual(verdicts, ["No differences observed across the countries run in this session."])


class TestTableRender(unittest.TestCase):
    def test_table_has_a_row_per_country_and_no_crash_on_missing_srv(self):
        rows = [srvprobe.SrvReducer(c).summary_row() for c in ("OE", "DL", "IT")]
        table = srvprobe.render_table(rows)
        for c in ("OE", "DL", "IT"):
            self.assertIn(c, table)


class TestParseOnly(unittest.TestCase):
    def test_parse_only_reduces_saved_logs(self):
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            oe = os.path.join(d, "OE.log")
            with open(oe, "w", encoding="utf-8") as fh:
                fh.write("2026-08-30 19:00:00 [GW];srv;OE;host;meshcom.oevsv.at;path;inet;ms;100\n")
                fh.write("2026-08-30 19:00:01 [GW];keep;tx;ok;1;ms;1000\n")
                fh.write("2026-08-30 19:00:01 [GW];rx;type;BEAT;len;22;ms;1050\n")
            paths = [oe]
            rows = []
            for p in paths:
                name = os.path.splitext(os.path.basename(p))[0].upper()
                red = srvprobe.SrvReducer(name)
                with open(p, encoding="utf-8", errors="replace") as fh:
                    for line in fh:
                        red.add(line[20:] if len(line) > 20 else line)
                rows.append(red.summary_row())
            self.assertEqual(rows[0]["country"], "OE")
            self.assertEqual(rows[0]["host"], "meshcom.oevsv.at")
            self.assertEqual(rows[0]["gw_rx_total"], 1)


if __name__ == "__main__":
    unittest.main()
