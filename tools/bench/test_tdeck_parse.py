#!/usr/bin/env python3
"""Unit tests for tools/bench/tdeck_parse.py -- one case per firmware line shape.

Run from this directory (tdeck_parse is imported by plain module name, so the
repo root is not a valid cwd) with either:
    cd tools/bench && python3 -m pytest test_tdeck_parse.py -q
    cd tools/bench && python3 -m unittest test_tdeck_parse
"""

from __future__ import annotations

import unittest

import math
import zlib

from tdeck_parse import (
    DISPTEST_BARS,
    DISPTEST_CIRCLE_MAX,
    DISPTEST_FILLS,
    DISPTEST_H,
    DISPTEST_PHASES,
    DISPTEST_PX,
    DISPTEST_SIN24,
    DISPTEST_SQUARE_MAX,
    DISPTEST_TRI_FRAMES,
    DISPTEST_TRI_STEPS,
    DISPTEST_W,
    disptest_crc,
    disptest_expected,
    disptest_frame,
    disptest_steps,
    disptest_triangle_vertices,
    heap_delta,
    parse_line,
    redraw_summary,
    refr_summary,
)


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

    def test_uistat_with_tft_sleeping_and_bl(self) -> None:
        line = (
            "[UISTAT];tab;2;drawer;0;objs;140;msg_list;12;inv_total;900;"
            "refr_total;300;last_refr_px;4096;last_refr_ms;9;redrawlog;1;"
            "heap_free;180000;heap_min;150000;psram_free;2000000;"
            "tft_sleeping;1;bl;0"
        )
        rec = parse_line(line)
        self.assertIsNotNone(rec)
        assert rec is not None
        self.assertEqual(rec["tft_sleeping"], 1)
        self.assertEqual(rec["bl"], 0)

    def test_uistat_unknown_extra_field_is_none(self) -> None:
        line = (
            "[UISTAT];tab;2;drawer;0;objs;140;msg_list;12;inv_total;900;"
            "refr_total;300;last_refr_px;4096;last_refr_ms;9;redrawlog;1;"
            "heap_free;180000;heap_min;150000;psram_free;2000000;bogus;1"
        )
        self.assertIsNone(parse_line(line))


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


class TestTft(unittest.TestCase):
    def test_tft_sleeping(self) -> None:
        rec = parse_line("[TFT];sleeping;1;bl;0;timer_age_ms;5230")
        self.assertEqual(
            rec, {"kind": "TFT", "sleeping": 1, "bl": 0, "timer_age_ms": 5230}
        )

    def test_tft_awake(self) -> None:
        rec = parse_line("[TFT];sleeping;0;bl;255;timer_age_ms;0")
        self.assertEqual(
            rec, {"kind": "TFT", "sleeping": 0, "bl": 255, "timer_age_ms": 0}
        )

    def test_tft_missing_field_is_none(self) -> None:
        self.assertIsNone(parse_line("[TFT];sleeping;1;bl;0"))


class TestScreen(unittest.TestCase):
    def test_screen_crc(self) -> None:
        line = (
            "[SCREEN];ms;12345;crc;0x1,0x2,0x3,0x4,0x5,0x6,0x7,0x8;"
            "nonblack;500;total;76800;t_ms;42"
        )
        rec = parse_line(line)
        self.assertEqual(
            rec,
            {
                "kind": "SCREEN",
                "variant": "crc",
                "ms": 12345,
                "crc": ["0x1", "0x2", "0x3", "0x4", "0x5", "0x6", "0x7", "0x8"],
                "nonblack": 500,
                "total": 76800,
                "t_ms": 42,
                "sleeping": None,
            },
        )

    def test_screen_crc_with_sleeping(self) -> None:
        line = (
            "[SCREEN];ms;1;crc;0x1,0x2,0x3,0x4,0x5,0x6,0x7,0x8;"
            "nonblack;0;total;76800;t_ms;1;sleeping;1"
        )
        rec = parse_line(line)
        self.assertIsNotNone(rec)
        assert rec is not None
        self.assertEqual(rec["sleeping"], 1)

    def test_screen_crc_wrong_count_is_none(self) -> None:
        line = "[SCREEN];ms;1;crc;0x1,0x2,0x3;nonblack;0;total;76800;t_ms;1"
        self.assertIsNone(parse_line(line))

    def test_screen_err(self) -> None:
        rec = parse_line("[SCREEN];err;capture_failed")
        self.assertEqual(rec, {"kind": "SCREEN", "variant": "err", "reason": "capture_failed"})


class TestBoot(unittest.TestCase):
    def test_boot_msg(self) -> None:
        rec = parse_line("[BOOT];msg;starting up")
        self.assertEqual(rec, {"kind": "BOOT", "variant": "msg", "text": "starting up"})

    def test_boot_msg_with_embedded_semicolons(self) -> None:
        rec = parse_line("[BOOT];msg;sd;ok;touch;ok")
        self.assertEqual(
            rec, {"kind": "BOOT", "variant": "msg", "text": "sd;ok;touch;ok"}
        )

    def test_boot_audio_file(self) -> None:
        rec = parse_line("[BOOT];audio;file;startup.wav")
        self.assertEqual(
            rec, {"kind": "BOOT", "variant": "audio", "what": "file", "file": "startup.wav"}
        )

    def test_boot_audio_cw(self) -> None:
        rec = parse_line("[BOOT];audio;cw")
        self.assertEqual(rec, {"kind": "BOOT", "variant": "audio", "what": "cw", "file": None})

    def test_boot_audio_none(self) -> None:
        rec = parse_line("[BOOT];audio;none")
        self.assertEqual(rec, {"kind": "BOOT", "variant": "audio", "what": "none", "file": None})

    def test_boot_init(self) -> None:
        line = "[BOOT];init;sd;1;touch;1;kb;0;psram_buf;1;t_ms;1234"
        rec = parse_line(line)
        self.assertEqual(
            rec,
            {
                "kind": "BOOT",
                "variant": "init",
                "sd": 1,
                "touch": 1,
                "kb": 0,
                "psram_buf": 1,
                "t_ms": 1234,
            },
        )

    def test_boot_init_touch_tries(self) -> None:
        # TM-33 (a): firmware from 2026-08-29 adds the GT911 begin() attempt count
        line = "[BOOT];init;sd;1;touch;1;touch_tries;3;kb;1;psram_buf;1;t_ms;3799"
        rec = parse_line(line)
        self.assertIsNotNone(rec)
        self.assertEqual(rec["touch_tries"], 3)
        self.assertEqual(rec["touch"], 1)

    def test_boot_init_rejects_unknown_key(self) -> None:
        self.assertIsNone(parse_line("[BOOT];init;sd;1;touch;1;bogus;3;kb;1;psram_buf;1;t_ms;1"))


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


# --------------------------------------------------------------------------
# TM-41 [DISPTEST]
# --------------------------------------------------------------------------


def _brute_frame(phase: str, i: int, stride: int = 1) -> bytes:
    """Per-pixel reference: a literal transcription of dt_render() in
    src/t-deck/tdeck_debug.cpp. Slow (0.3 s a frame), so it is only used to
    prove that the span renderer in tdeck_parse.py picks the same pixels."""
    w, h_res = DISPTEST_W, DISPTEST_H
    bg, fg = 0x0000, 0xFFFF
    h = r4 = 0
    v = [(0, 0)] * 3
    if phase == "square":
        bg, fg = 0xFFFF, 0x0000
        h = 2 * min(DISPTEST_SQUARE_MAX, (i + 1) * stride)
    elif phase == "circle":
        r = min(DISPTEST_CIRCLE_MAX, (i + 1) * stride)
        r4 = (2 * r) * (2 * r)
    elif phase == "triangle":
        v = disptest_triangle_vertices(i)
    elif phase == "colors":
        c = DISPTEST_FILLS[i % 5]
        bg = (~c) & 0xFFFF if i >= 5 else c
    out = bytearray()
    for y in range(h_res):
        yy = 2 * y - (h_res - 1)
        rbg = bg
        if phase == "invert":
            rbg = DISPTEST_BARS[y // (h_res // 8)]
            if i == 1:
                rbg = (~rbg) & 0xFFFF
        for x in range(w):
            xx = 2 * x - (w - 1)
            if phase == "square":
                inside = abs(xx) <= h and abs(yy) <= h
            elif phase == "circle":
                inside = xx * xx + yy * yy <= r4
            elif phase == "triangle":
                inside = True
                for k in range(3):
                    (vx1, vy1), (vx2, vy2) = v[k], v[(k + 1) % 3]
                    if (vx2 - vx1) * (yy - vy1) - (vy2 - vy1) * (xx - vx1) < 0:
                        inside = False
                        break
            else:
                inside = False
            c = fg if inside else rbg
            out += bytes(((c >> 8) & 0xFF, c & 0xFF))
    return bytes(out)


class TestDispTestParse(unittest.TestCase):
    def test_begin(self) -> None:
        rec = parse_line(
            "[DISPTEST];begin;phase;full;stride;1;w;320;h;240;steps;516;ms;12345"
        )
        self.assertEqual(
            rec,
            {
                "kind": "DISPTEST",
                "variant": "begin",
                "phase": "full",
                "stride": 1,
                "w": 320,
                "h": 240,
                "steps": 516,
                "ms": 12345,
            },
        )

    def test_step(self) -> None:
        rec = parse_line("[DISPTEST];step;circle;n;17;crc;0a1b2c3d;px;76800;ms;38")
        self.assertEqual(
            rec,
            {
                "kind": "DISPTEST",
                "variant": "step",
                "phase": "circle",
                "n": 17,
                "crc": 0x0A1B2C3D,
                "px": 76800,
                "ms": 38,
            },
        )

    def test_step_glued_to_command_echo(self) -> None:
        rec = parse_line("--disptest full 1[DISPTEST];step;invert;n;0;crc;ffffffff;px;76800;ms;40")
        assert rec is not None
        self.assertEqual(rec["crc"], 0xFFFFFFFF)
        self.assertEqual(rec["phase"], "invert")

    def test_end_and_err(self) -> None:
        self.assertEqual(
            parse_line("[DISPTEST];end;steps;516;ms;23456"),
            {"kind": "DISPTEST", "variant": "end", "steps": 516, "ms": 23456},
        )
        self.assertEqual(
            parse_line("[DISPTEST];err;phase;wobble"),
            {"kind": "DISPTEST", "variant": "err", "reason": "phase", "detail": "wobble"},
        )

    def test_malformed_lines_are_none(self) -> None:
        for line in (
            "[DISPTEST]",
            "[DISPTEST];step;circle;n;17;crc;0a1b2c3d;px;76800",       # short
            "[DISPTEST];step;circle;n;17;crc;xyz;px;76800;ms;38",      # crc not hex
            "[DISPTEST];step;circle;n;17;crc;0a1b2c3;px;76800;ms;38",  # crc too short
            "[DISPTEST];step;circle;i;17;crc;0a1b2c3d;px;76800;ms;38",  # wrong key
            "[DISPTEST];end;steps;516",
            "[DISPTEST];begin;phase;full;stride;1;w;320;h;240;steps;516",
        ):
            self.assertIsNone(parse_line(line), line)


class TestDispTestRender(unittest.TestCase):
    def test_sin_table_matches_its_formula(self) -> None:
        self.assertEqual(len(DISPTEST_SIN24), DISPTEST_TRI_STEPS)
        for i, v in enumerate(DISPTEST_SIN24):
            self.assertEqual(v, round(1024 * math.sin(2 * math.pi * i / DISPTEST_TRI_STEPS)), i)

    def test_step_counts(self) -> None:
        self.assertEqual([disptest_steps(p) for p in DISPTEST_PHASES], [2, 10, 160, 200, 144])
        self.assertEqual(disptest_steps("square", 4), 40)
        self.assertEqual(disptest_steps("circle", 7), 29)     # ceil(200/7)
        self.assertEqual(disptest_steps("triangle", 4), 144)  # stride does not apply
        with self.assertRaises(ValueError):
            disptest_steps("wobble")

    def test_frame_size_and_solid_fill_crc(self) -> None:
        """The five fills are the byte-order anchor: red must be f8,00 per pixel."""
        frame = disptest_frame("colors", 0)
        self.assertEqual(len(frame), DISPTEST_PX * 2)
        self.assertEqual(frame, b"\xf8\x00" * DISPTEST_PX)
        self.assertEqual(disptest_crc("colors", 0), zlib.crc32(b"\xf8\x00" * DISPTEST_PX))

    def test_inverted_pass_is_the_complement(self) -> None:
        for i in range(5):
            a = disptest_frame("colors", i)
            b = disptest_frame("colors", i + 5)
            self.assertEqual(bytes(x ^ 0xFF for x in a[:2]), b[:2])
            self.assertEqual(b, bytes(x ^ 0xFF for x in a[:2]) * DISPTEST_PX)

    def test_invert_pass_is_bars_then_complement(self) -> None:
        a = disptest_frame("invert", 0)
        b = disptest_frame("invert", 1)
        self.assertEqual(b, bytes(x ^ 0xFF for x in a))
        band = DISPTEST_H // 8
        for k, colour in enumerate(DISPTEST_BARS):
            off = (k * band) * DISPTEST_W * 2
            self.assertEqual(a[off : off + 2], bytes(((colour >> 8) & 0xFF, colour & 0xFF)))

    def test_growth_starts_at_the_centre_and_ends_full_screen(self) -> None:
        # first step: a 2x2 block of foreground at the exact centre
        self.assertEqual(disptest_frame("square", 0).count(b"\x00\x00"), 4)
        self.assertEqual(disptest_frame("circle", 0).count(b"\xff\xff"), 4)
        # last step: the shape covers every pixel
        self.assertEqual(disptest_frame("square", 159), b"\x00\x00" * DISPTEST_PX)
        self.assertEqual(disptest_frame("circle", 199), b"\xff\xff" * DISPTEST_PX)
        # ... and only there: one step earlier a corner is still background
        self.assertNotEqual(disptest_frame("circle", 198)[:2], b"\xff\xff")

    def test_shapes_are_point_symmetric_about_the_centre(self) -> None:
        for phase, i in (("square", 40), ("circle", 60)):
            frame = disptest_frame(phase, i)
            rows = [frame[y * DISPTEST_W * 2 : (y + 1) * DISPTEST_W * 2] for y in range(DISPTEST_H)]
            for y, row in enumerate(rows):
                mirror = rows[DISPTEST_H - 1 - y]
                px = [row[2 * x : 2 * x + 2] for x in range(DISPTEST_W)]
                self.assertEqual(
                    b"".join(reversed(px)), mirror, f"{phase} step {i} row {y}"
                )

    def test_triangle_rotates_three_turns_each_way(self) -> None:
        v0 = disptest_triangle_vertices(0)
        # one full turn later the vertex set repeats
        self.assertEqual(sorted(v0), sorted(disptest_triangle_vertices(DISPTEST_TRI_STEPS)))
        # the two halves are mirror images in time: frame 71 (last clockwise)
        # and frame 72 (first counter-clockwise) share an angle index
        self.assertEqual(
            sorted(disptest_triangle_vertices(71)), sorted(disptest_triangle_vertices(72))
        )
        self.assertEqual(len(disptest_expected("triangle")), DISPTEST_TRI_FRAMES)
        # vertices sit on the r=100 px circle (200 in doubled coordinates)
        for vx, vy in v0:
            self.assertLessEqual(abs(math.hypot(vx, vy) - 200), 1.5)

    def test_span_renderer_matches_the_firmware_pixel_test(self) -> None:
        """The contract with dt_render(): same pixels, one CRC per frame."""
        for phase, i, stride in (
            ("invert", 1, 1),
            ("colors", 7, 1),
            ("square", 0, 1),
            ("square", 39, 4),
            ("circle", 5, 1),
            ("circle", 49, 4),
            ("triangle", 0, 1),
            ("triangle", 5, 1),
            ("triangle", 143, 1),
        ):
            with self.subTest(phase=phase, i=i, stride=stride):
                self.assertEqual(disptest_frame(phase, i, stride), _brute_frame(phase, i, stride))

    def test_expected_covers_the_whole_sequence_in_order(self) -> None:
        exp = disptest_expected("full", 1)
        self.assertEqual(len(exp), 516)
        self.assertEqual([p for p, _, _ in exp[:2]], ["invert", "invert"])
        self.assertEqual(exp[-1][0], "triangle")
        self.assertEqual([n for _, n, _ in exp[2:12]], list(range(10)))
        # every CRC is the CRC of that frame, and the growth frames all differ
        self.assertEqual(exp[3][2], disptest_crc("colors", 1))
        square = [c for p, _, c in exp if p == "square"]
        self.assertEqual(len(set(square)), len(square))

    def test_out_of_range_step_raises(self) -> None:
        with self.assertRaises(ValueError):
            disptest_frame("square", 160)
        with self.assertRaises(ValueError):
            disptest_frame("colors", -1)


if __name__ == "__main__":
    unittest.main()
