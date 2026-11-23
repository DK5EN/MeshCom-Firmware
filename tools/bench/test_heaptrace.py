"""Tests for heaptrace.py (synthetic logs, addr2line mocked)."""
from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import heaptrace  # noqa: E402

P = "2026-11-23 10:00:00.000  "


def a(t, ptr, size, pc1="40001000", pc2="40002000", pc3="40003000", task="loop"):
    return f"{P}[HT];a;{t};{ptr};{size};{task};{pc1};{pc2};{pc3}"


def f(t, ptr, pc="40001000"):
    return f"{P}[HT];f;{t};{ptr};0;loop;{pc};00000000;00000000"


def r(t, new, size, old, pc="40001000"):
    return f"{P}[HT];r;{t};{new};{size};loop;{pc};00000000;00000000;{old}"


def m(t, name, ifree, iblk=1, drops=0):
    return f"{P}[HT];m;{t};{name};ifree;{ifree};iblk;{iblk};drops;{drops}"


FAKE = {0x40001000: ("malloc", "a.c:1"), 0x40002000: ("Foo::bar", "foo.cpp:10"),
        0x40003000: ("main_fn", "main.cpp:5")}


def fake_resolve(pcs, elf, prefix=None):
    return {p: FAKE.get(p, ("??", "??:0")) for p in pcs}


def seg_of(lines):
    return heaptrace.parse_log(lines)[-1]


def test_alloc_free_realloc():
    s = seg_of([a(1, "20000000", 100), a(2, "20000100", 50), f(3, "20000000"),
                r(4, "20000200", 80, "20000100")])
    assert {k: b.size for k, b in s.live.items()} == {0x20000200: 80}


def test_failed_realloc_keeps_old():
    s = seg_of([a(1, "20000000", 100), r(2, "00000000", 200, "20000000")])
    assert 0x20000000 in s.live and s.failed_realloc == 1


def test_unknown_free_and_null_free():
    s = seg_of([f(1, "20000999"), f(2, "00000000")])
    assert s.unmatched_free == 1 and not s.live


def test_double_alloc_replaces():
    s = seg_of([a(1, "20000000", 10), a(2, "20000000", 30)])
    assert s.double_alloc == 1 and s.live[0x20000000].size == 30


def test_reboot_resets():
    segs = heaptrace.parse_log([a(1, "20000000", 10), "ets Jun  8 rst:0x1 (POWERON)",
                                a(2, "20000100", 5), "ESP-ROM:esp32s3-20210327", a(3, "20000200", 7)])
    assert len(segs) == 3
    assert list(segs[0].live) == [0x20000000] and list(segs[2].live) == [0x20000200]


def test_ignores_other_lines():
    segs = heaptrace.parse_log(["hello", P + "[HT];x;bad", P + "[HT];a;1"])
    assert len(segs) == 1 and not segs[0].live


def test_markers_grouping_sums_and_delta(monkeypatch):
    monkeypatch.setattr(heaptrace, "resolve_pcs", fake_resolve)
    s = seg_of([m(0, "m0", 10000),
                a(1, "20000000", 100), a(2, "20000100", 60, pc2="40003000"),
                a(3, "20000200", 40, pc1="40003000"), m(4, "m1", 10000 - 200 - 3 * 8, 3)])
    reps = heaptrace.analyse(s, None, heaptrace.DEFAULT_SKIP_FN, "x.elf")
    assert [x["marker"] for x in reps] == ["m0", "m1"]
    r1 = reps[1]
    assert r1["live_total"] == 200
    bc = {x["function"]: x["bytes"] for x in r1["by_caller"]}
    assert bc == {"malloc": 160, "main_fn": 40}
    # malloc is skipped; Foo::bar is not (no default pattern matches it)
    bo = {x["function"]: (x["bytes"], x["blocks"]) for x in r1["by_first_outside_allocator"]}
    assert sum(v[0] for v in bo.values()) == 200
    assert bo == {"Foo::bar": (100, 1), "main_fn": (100, 2)}
    d = r1["delta"]
    assert d["live"] == 200 and d["ifree_drop"] == 224 and d["blocks"] == 3
    assert d["overhead_per_block"] == 8.0


def test_skip_fn_custom(monkeypatch):
    monkeypatch.setattr(heaptrace, "resolve_pcs", fake_resolve)
    s = seg_of([a(1, "20000000", 100), m(2, "m", 5000)])
    reps = heaptrace.analyse(s, None, r"^(malloc|main_fn)$", "x.elf")
    assert reps[0]["by_first_outside_allocator"][0]["function"] == "Foo::bar"


def test_at_filter_and_drops_warning(monkeypatch, tmp_path, capsys):
    monkeypatch.setattr(heaptrace, "resolve_pcs", fake_resolve)
    log = tmp_path / "x.log"
    log.write_text("\n".join([a(1, "20000000", 10), m(2, "keep", 900, 1, 0), m(3, "other", 800, 1, 4)]))
    out = tmp_path / "o.json"
    assert heaptrace.main([str(log), "--at", "other", "--json", str(out)]) == 0
    text = capsys.readouterr().out
    assert "WARNING" in text and "incomplete" in text and "marker keep" not in text
    import json
    assert json.loads(out.read_text())["markers"][0]["drops"] == 4


def test_no_warning_without_drops(monkeypatch, tmp_path, capsys):
    monkeypatch.setattr(heaptrace, "resolve_pcs", fake_resolve)
    log = tmp_path / "x.log"
    log.write_text("\n".join([a(1, "20000000", 10), m(2, "k", 900)]))
    heaptrace.main([str(log)])
    assert "WARNING" not in capsys.readouterr().out


def test_resolver_missing_toolchain_falls_back(monkeypatch):
    monkeypatch.setattr(heaptrace, "find_addr2line", lambda prefix: None)
    assert heaptrace.resolve_pcs([0x40001000], "x.elf") == {0x40001000: ("??", "??:0")}


def test_realloc_address_reuse_race():
    # another core's "a X" arrives before the "r Y<-X" line of the realloc that freed X
    s = seg_of([a(1, "20000000", 100, task="t1"), a(2, "20000000", 40, task="t2"),
                r(3, "20000300", 150, "20000000"), f(4, "20000000")])
    assert {k: b.size for k, b in s.live.items()} == {0x20000300: 150}
    assert s.shadowed == 1 and not s.shadow and s.unmatched_free == 0 and s.unmatched_realloc == 0


def test_realloc_race_keeps_new_owner_until_its_free():
    s = seg_of([a(1, "20000000", 100, task="t1"), a(2, "20000000", 40, task="t2"),
                r(3, "20000300", 150, "20000000")])
    assert {k: (b.size, b.task) for k, b in s.live.items()} == {0x20000000: (40, "t2"),
                                                                0x20000300: (150, "loop")}


def test_dropped_free_plain_free_releases_the_live_owner():
    # "f X" of the first block was dropped. A plain "f" is written before the real free, so it is never late:
    # it releases the live owner (30 B); the 10 B block stays as a residual shadow, visible in the report.
    s = seg_of([a(1, "20000000", 10), a(2, "20000000", 30), f(3, "20000000")])
    assert s.shadowed == 1 and s.double_alloc == 1
    assert not s.live
    assert {k: [b.size for b in v] for k, v in s.shadow.items()} == {0x20000000: [10]}


def test_moved_out_realloc_releases_the_shadow_first():
    # realloc moved X (internal) to PSRAM: the old block's "o" line can arrive after another task reused X
    s = seg_of([a(1, "20000000", 100, task="t1"), a(2, "20000000", 40, task="t2"), P + "[HT];o;3;20000000;0;t1;40001000;40002000;40003000"])
    assert {k: (b.size, b.task) for k, b in s.live.items()} == {0x20000000: (40, "t2")}
    assert not s.shadow


def test_shadow_count_in_report():
    s = seg_of([a(1, "20000000", 10), a(2, "20000000", 30)])
    assert "shadowed 1" in heaptrace.render(s, [], 5)


def test_boot_marker_needs_rst_or_leading_rom():
    segs = heaptrace.parse_log([a(1, "20000000", 10), P + "first: something", P + "a line with first:0x1",
                                a(2, "20000100", 5)])
    assert len(segs) == 1 and len(segs[0].live) == 2
    segs = heaptrace.parse_log([a(1, "20000000", 10), P + "ESP-ROM:esp32s3-20210327", a(2, "20000100", 5)])
    assert len(segs) == 2
    segs = heaptrace.parse_log([a(1, "20000000", 10), "x ESP-ROM:nope", a(2, "20000100", 5)])
    assert len(segs) == 1


def test_consecutive_boot_markers_make_one_segment():
    segs = heaptrace.parse_log([a(1, "20000000", 10), "ets rst:0x1 (POWERON)", "ESP-ROM:esp32s3-20210327",
                                "rst:0x3 again", a(2, "20000100", 5)])
    assert len(segs) == 2 and list(segs[1].live) == [0x20000100]
    segs = heaptrace.parse_log(["rst:0x1 (POWERON)", "ESP-ROM:x", a(1, "20000000", 10)])
    assert len(segs) == 1


def test_default_skip_fn_word_boundary_new():
    skip = __import__("re").compile(heaptrace.DEFAULT_SKIP_FN)
    for name in ("renew", "newMessage", "Foo::renewal", "my_newbuf"):
        assert not skip.search(name), name
    for name in ("operator new(unsigned int)", "operator new[](unsigned int)", "new", "malloc"):
        assert skip.search(name), name


def test_addr2line_queries_pc_minus_3(monkeypatch):
    seen = {}

    class R:
        stdout = "fn_a\na.c:1\nfn_b\nb.c:2\n"

    def fake_run(cmd, **kw):
        seen["cmd"] = cmd
        return R()

    monkeypatch.setattr(heaptrace, "find_addr2line", lambda prefix: "addr2line")
    monkeypatch.setattr(heaptrace.subprocess, "run", fake_run)
    out = heaptrace.resolve_pcs([0x40002000, 0x40001000, 0x40001000], "x.elf")
    assert seen["cmd"][-2:] == ["0x40000ffd", "0x40001ffd"]
    assert out == {0x40001000: ("fn_a", "a.c:1"), 0x40002000: ("fn_b", "b.c:2")}


def test_drain_stack_info_and_unknown_kinds():
    line = P + "[HT];i;5;drain;stack;1234"
    s = seg_of([a(1, "20000000", 10), line, P + "[HT];i;6;whatever;x;9", P + "[HT];z;7;q;r;s;t",
                P + "[HT];i;8;drain;stack;66"])
    assert s.untraced_bytes == 1300 and list(s.live) == [0x20000000]
    assert "untraced bytes (drain stack) 1300" in heaptrace.render(s, [], 5)
