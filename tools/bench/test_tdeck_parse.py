#!/usr/bin/env python3
"""Unit tests for tools/bench/tdeck_parse.py -- one case per firmware line shape.

Run with either:
    python3 -m pytest tools/bench/test_tdeck_parse.py -q
    python3 -m unittest tools/bench/test_tdeck_parse.py
"""

from __future__ import annotations

import unittest

from tdeck_parse import heap_delta, parse_line, redraw_summary, refr_summary


class TestRedraw(unittest.TestCase):
    def test_redraw_obj_with_name(self) -> None:
        line = (
            "[REDRAW];ms;12345;obj;0xDEAD0001;cls;lv_label;"
            "area;0;0;100;20;ra;0x420081AB;name;msg_list"
        )
        rec = parse_line(line)
        self.assertEqual(
            rec,
            {
                "kind": "REDRAW",
                "variant": "obj",
                "ms": 12345,
                "obj": "0xDEAD0001",
                "cls": "lv_label",
                "area": (0, 0, 100, 20),
                "ra": "0x420081AB",
                "bt": [],
                "name": "msg_list",
            },
        )

    def test_redraw_obj_with_backtrace_and_name(self) -> None:
        line = (
            "[REDRAW];ms;7;obj;0x3d828224;cls;label;area;219;-1;267;32;"
            "ra;0x4207c5e8;bt;0x42001111,0x42002222,0x42003333;name;tv"
        )
        rec = parse_line(line)
        self.assertIsNotNone(rec)
        assert rec is not None
        self.assertEqual(rec["bt"], ["0x42001111", "0x42002222", "0x42003333"])
        self.assertEqual(rec["name"], "tv")

    def test_redraw_obj_with_empty_backtrace(self) -> None:
        line = "[REDRAW];ms;7;obj;0x1;cls;label;area;0;0;1;1;ra;0x2;bt;-"
        rec = parse_line(line)
        self.assertIsNotNone(rec)
        assert rec is not None
        self.assertEqual(rec["bt"], [])

    def test_line_glued_to_command_echo(self) -> None:
        line = (
            "--uistat[UISTAT];tab;0;drawer;0;objs;164;msg_list;1;inv_total;842;"
            "refr_total;21;last_refr_px;76800;last_refr_ms;57;redrawlog;0;"
            "heap_free;87300;heap_min;85712;psram_free;8005535"
        )
        rec = parse_line(line)
        self.assertIsNotNone(rec)
        assert rec is not None
        self.assertEqual(rec["kind"], "UISTAT")
        self.assertEqual(rec["objs"], 164)

    def test_redraw_obj_without_name(self) -> None:
        line = "[REDRAW];ms;1;obj;0x1;cls;lv_btn;area;1;2;3;4;ra;0x2"
        rec = parse_line(line)
        self.assertIsNotNone(rec)
        assert rec is not None
        self.assertEqual(rec["variant"], "obj")
        self.assertIsNone(rec["name"])
        self.assertEqual(rec["area"], (1, 2, 3, 4))

    def test_redraw_dropped(self) -> None:
        rec = parse_line("[REDRAW];dropped;7")
        self.assertEqual(rec, {"kind": "REDRAW", "variant": "dropped", "dropped": 7})

    def test_redraw_trailing_semicolon(self) -> None:
        line = "[REDRAW];ms;1;obj;0x1;cls;lv_btn;area;1;2;3;4;ra;0x2;"
        rec = parse_line(line)
        self.assertIsNotNone(rec)

    def test_redraw_malformed_returns_none(self) -> None:
        self.assertIsNone(parse_line("[REDRAW];ms;notanumber;obj;0x1"))
        self.assertIsNone(parse_line("[REDRAW];bogus;stuff"))


class TestRefr(unittest.TestCase):
    def test_refr(self) -> None:
        rec = parse_line("[REFR];ms;500;px;12000;t_ms;18")
        self.assertEqual(rec, {"kind": "REFR", "ms": 500, "px": 12000, "t_ms": 18})

    def test_refrstart(self) -> None:
        rec = parse_line("[REFRSTART];ms;500;areas;3")
        self.assertEqual(rec, {"kind": "REFRSTART", "ms": 500, "areas": 3})


class TestUistat(unittest.TestCase):
    def test_uistat(self) -> None:
        line = (
            "[UISTAT];tab;2;drawer;0;objs;140;msg_list;12;inv_total;900;"
            "refr_total;300;last_refr_px;4096;last_refr_ms;9;redrawlog;1;"
            "heap_free;180000;heap_min;150000;psram_free;2000000"
        )
        rec = parse_line(line)
        self.assertIsNotNone(rec)
        assert rec is not None
        self.assertEqual(rec["kind"], "UISTAT")
        self.assertEqual(rec["tab"], 2)
        self.assertEqual(rec["drawer"], 0)
        self.assertEqual(rec["heap_free"], 180000)
        self.assertEqual(rec["psram_free"], 2000000)

    def test_uistat_missing_field_is_none(self) -> None:
        # exact key-set required; a short line must not silently half-parse
        self.assertIsNone(parse_line("[UISTAT];tab;2;drawer;0"))


class TestTab(unittest.TestCase):
    def test_tab_line(self) -> None:
        rec = parse_line("[TAB];3;map")
        self.assertEqual(rec, {"kind": "TAB", "variant": "line", "idx": 3, "name": "map"})

    def test_tab_active(self) -> None:
        rec = parse_line("[TAB];active;3")
        self.assertEqual(rec, {"kind": "TAB", "variant": "active", "idx": 3})

    def test_tab_set(self) -> None:
        rec = parse_line("[TAB];set;3;inv_delta;42;")
        self.assertEqual(
            rec, {"kind": "TAB", "variant": "set", "idx": 3, "inv_delta": 42}
        )

    def test_tab_err(self) -> None:
        rec = parse_line("[TAB];err;range")
        self.assertEqual(rec, {"kind": "TAB", "variant": "err", "reason": "range"})


class TestDrawer(unittest.TestCase):
    def test_drawer_open(self) -> None:
        self.assertEqual(parse_line("[DRAWER];1"), {"kind": "DRAWER", "state": 1})

    def test_drawer_closed(self) -> None:
        self.assertEqual(parse_line("[DRAWER];0"), {"kind": "DRAWER", "state": 0})


class TestInject(unittest.TestCase):
    def test_inject_ok(self) -> None:
        line = "[INJECT];ok;id;0xABCD1234;dst;9999;src;DK5EN-14;len;23"
        rec = parse_line(line)
        self.assertEqual(
            rec,
            {
                "kind": "INJECT",
                "variant": "ok",
                "id": "0xABCD1234",
                "dst": "9999",
                "src": "DK5EN-14",
                "len": 23,
            },
        )

    def test_inject_err(self) -> None:
        rec = parse_line("[INJECT];err;queue_full")
        self.assertEqual(rec, {"kind": "INJECT", "variant": "err", "reason": "queue_full"})


class TestAudio(unittest.TestCase):
    def test_audio_play_no_vol(self) -> None:
        rec = parse_line("[AUDIO];play;start")
        self.assertEqual(rec, {"kind": "AUDIO", "variant": "play", "what": "start", "vol": None})

    def test_audio_play_with_vol(self) -> None:
        rec = parse_line("[AUDIO];play;msg;vol;80")
        self.assertEqual(rec, {"kind": "AUDIO", "variant": "play", "what": "msg", "vol": 80})

    def test_audio_err(self) -> None:
        rec = parse_line("[AUDIO];err;missing;/nonexistent.wav")
        self.assertEqual(
            rec,
            {"kind": "AUDIO", "variant": "err", "err_kind": "missing", "detail": "/nonexistent.wav"},
        )

    def test_audio_info(self) -> None:
        rec = parse_line("[AUDIO];info;playing tone")
        self.assertEqual(rec, {"kind": "AUDIO", "variant": "info", "text": "playing tone"})

    def test_audio_eof(self) -> None:
        rec = parse_line("[AUDIO];eof;/sd/msg.wav")
        self.assertEqual(rec, {"kind": "AUDIO", "variant": "eof", "file": "/sd/msg.wav"})


class TestInstr(unittest.TestCase):
    def test_instr_heap(self) -> None:
        line = (
            "[INSTR-HEAP];h0;int_free;180000;int_min;150000;int_largest;90000;"
            "psram_free;2000000;psram_largest;1900000"
        )
        rec = parse_line(line)
        self.assertEqual(
            rec,
            {
                "kind": "INSTR-HEAP",
                "tag": "h0",
                "int_free": 180000,
                "int_min": 150000,
                "int_largest": 90000,
                "psram_free": 2000000,
                "psram_largest": 1900000,
            },
        )

    def test_instr_flush(self) -> None:
        rec = parse_line("[INSTR-FLUSH];n;40;total_us;12000;avg_us;300;max_us;900")
        self.assertEqual(
            rec,
            {"kind": "INSTR-FLUSH", "n": 40, "total_us": 12000, "avg_us": 300, "max_us": 900},
        )

    def test_instr_loop(self) -> None:
        rec = parse_line("[INSTR-LOOP];n;1000;total_us;500000;avg_us;500;max_us;4200")
        self.assertEqual(
            rec,
            {"kind": "INSTR-LOOP", "n": 1000, "total_us": 500000, "avg_us": 500, "max_us": 4200},
        )

    def test_instr_gui(self) -> None:
        line = "[INSTR-GUI];msg_list_children;12;active_tab_bubbles;5;persisted_msgs;12;map_points;3"
        rec = parse_line(line)
        self.assertEqual(
            rec,
            {
                "kind": "INSTR-GUI",
                "msg_list_children": 12,
                "active_tab_bubbles": 5,
                "persisted_msgs": 12,
                "map_points": 3,
            },
        )


class TestUnknown(unittest.TestCase):
    def test_unrelated_log_line(self) -> None:
        self.assertIsNone(parse_line("I (1234) wifi: connected"))

    def test_unknown_bracket_tag(self) -> None:
        self.assertIsNone(parse_line("[NOTAREALTAG];foo;bar"))

    def test_empty_line(self) -> None:
        self.assertIsNone(parse_line(""))

    def test_bracket_no_close(self) -> None:
        self.assertIsNone(parse_line("[REDRAW;ms;1"))


class TestAggregates(unittest.TestCase):
    def setUp(self) -> None:
        self.records = [
            parse_line("[REDRAW];ms;1;obj;0x1;cls;lv_label;area;0;0;10;10;ra;0xAAA;name;msg_list"),
            parse_line("[REDRAW];ms;2;obj;0x2;cls;lv_label;area;0;0;10;10;ra;0xAAA;name;msg_list"),
            parse_line("[REDRAW];ms;3;obj;0x3;cls;lv_btn;area;0;0;10;10;ra;0xBBB"),
            parse_line("[REDRAW];dropped;4"),
            parse_line("[REFR];ms;1;px;100;t_ms;5"),
            parse_line("[REFR];ms;2;px;200;t_ms;15"),
            parse_line("not a firmware line at all"),
            None,
        ]

    def test_redraw_summary(self) -> None:
        s = redraw_summary(self.records)
        self.assertEqual(s["total"], 3)
        self.assertEqual(s["dropped"], 4)
        self.assertEqual(s["by_cls_name"][("lv_label", "msg_list")], 2)
        self.assertEqual(s["by_cls_name"][("lv_btn", None)], 1)
        self.assertEqual(s["by_ra"]["0xAAA"], 2)
        self.assertEqual(s["by_ra"]["0xBBB"], 1)

    def test_refr_summary(self) -> None:
        s = refr_summary(self.records, window_seconds=2.0)
        self.assertEqual(s["count"], 2)
        self.assertEqual(s["sum_px"], 300)
        self.assertEqual(s["mean_t_ms"], 10.0)
        self.assertEqual(s["max_t_ms"], 15)
        self.assertEqual(s["refreshes_per_second"], 1.0)
        self.assertEqual(s["px_per_second"], 150.0)

    def test_refr_summary_empty(self) -> None:
        s = refr_summary([], window_seconds=10.0)
        self.assertEqual(s["count"], 0)
        self.assertEqual(s["sum_px"], 0)
        self.assertEqual(s["mean_t_ms"], 0.0)
        self.assertEqual(s["max_t_ms"], 0)


class TestHeapDelta(unittest.TestCase):
    def test_uistat_style(self) -> None:
        first = {"heap_free": 180000, "heap_min": 150000, "psram_free": 2000000}
        last = {"heap_free": 175000, "heap_min": 148000, "psram_free": 1990000}
        d = heap_delta(first, last)
        self.assertEqual(d, {"heap_free": -5000, "heap_min": -2000, "psram_free": -10000})

    def test_instr_heap_style(self) -> None:
        first = parse_line(
            "[INSTR-HEAP];h0;int_free;180000;int_min;150000;int_largest;90000;"
            "psram_free;2000000;psram_largest;1900000"
        )
        last = parse_line(
            "[INSTR-HEAP];h1;int_free;170000;int_min;145000;int_largest;85000;"
            "psram_free;1980000;psram_largest;1880000"
        )
        d = heap_delta(first, last)
        self.assertEqual(
            d,
            {
                "int_free": -10000,
                "int_min": -5000,
                "int_largest": -5000,
                "psram_free": -20000,
                "psram_largest": -20000,
            },
        )

    def test_none_inputs(self) -> None:
        self.assertEqual(heap_delta(None, {"heap_free": 1}), {})
        self.assertEqual(heap_delta({"heap_free": 1}, None), {})


if __name__ == "__main__":
    unittest.main()
