"""Tests for heaptimeline.py (host only, synthetic fixtures)."""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import heaptimeline as ht  # noqa: E402

FIX = Path(__file__).resolve().parent / "testdata" / "heaptimeline"
SEMI, SPACES = FIX / "semi.log", FIX / "spaces.log"


def test_parse_semicolon_style() -> None:
    d = ht.parse_logs([SEMI])
    assert len(d.samples) == 5
    assert d.samples[0].ifree == 100000 and d.samples[0].iblk == 80000 and d.samples[0].imin == 90000


def test_parse_space_style() -> None:
    d = ht.parse_logs([SPACES])
    assert [s.ifree for s in d.samples] == [100000, 70000]
    assert d.sites["7"].pcs == [0x400D1000, 0x400D2000]
    assert d.map is not None and d.map.summary["free"] == 3584
    assert d.probes[0].name == "p1" and d.probes[0].t_end is None


def test_reboot_detection() -> None:
    d = ht.parse_logs([SEMI])
    assert len(d.reboots) == 1
    assert [s.boot for s in d.samples] == [0, 0, 0, 0, 1]


def test_steps_derived_from_heap() -> None:
    d = ht.parse_logs([SPACES], 5120)
    kinds = {(s.kind, s.direction) for s in d.steps}
    assert kinds == {("ifree", "down"), ("imin", "down")}
    d2 = ht.derive_steps(ht.parse_logs([SEMI]).samples, 5120)
    # 99000 -> 60000 ifree, imin 90000 -> 50000, iblk 80000 -> 40000; reboot jump ignored
    assert sorted(s.kind for s in d2 if s.direction == "down") == ["iblk", "ifree", "imin"]
    assert not [s for s in d2 if s.direction == "up"]  # boot change not a step
    assert all(s.delta <= -5120 for s in d2)


def test_step_threshold() -> None:
    assert ht.derive_steps(ht.parse_logs([SEMI]).samples, 50000) == []


def test_instr_step_replaces_derived() -> None:
    d = ht.parse_logs([SEMI])
    assert len(d.steps) == 1
    s = d.steps[0]
    assert (s.site, s.task, s.op, s.delta, s.src) == ("3", "tiT", "alloc", -6000, "instr")


def test_site_never_freed() -> None:
    d = ht.parse_logs([SEMI])
    assert d.sites["3"].never_freed
    assert not d.sites["4"].never_freed   # frees > 0
    assert not d.sites["5"].never_freed   # live == 0


def test_map_summary_and_blocks() -> None:
    m = ht.parse_logs([SEMI]).map
    assert m is not None
    assert m.summary == {"used": 1280, "untracked": 256, "free": 2816, "holes": 1, "largest": 2816, "drops": 0}
    assert len(m.regions) == 1 and m.regions[0]["start"] == 0x3FC90000
    assert [b["kind"] for b in m.blocks] == ["used", "free", "used"]


def test_probe_parsing() -> None:
    p = ht.parse_logs([SEMI]).probes[0]
    assert (p.name, p.low, p.peak_used, p.delta, p.dt_ms, p.rc) == ("ota", 45000, 15000, -10000, 5000, "0")
    assert p.start_ifree == 60000 and p.end_iblk == 30000


def test_au_events() -> None:
    d = ht.parse_logs([SEMI])
    assert any(e.kind == "au" and e.label.startswith("AU check") for e in d.events)


def test_html_has_svg_and_sections(tmp_path: Path) -> None:
    out, csv_out = tmp_path / "r.html", tmp_path / "r.csv"
    assert ht.main([str(SEMI), "--out", str(out), "--csv", csv_out.as_posix()]) == 0
    text = out.read_text()
    assert "<svg" in text and "prefers-color-scheme" in text and "http" not in text.replace("http://www.w3.org/2000/svg", "")
    assert "never freed" in text and "Heap map" in text
    assert csv_out.read_text().splitlines()[0] == "time,up_s,ifree,imin,iblk"
    assert len(csv_out.read_text().splitlines()) == 6


def test_addr2line_parse() -> None:
    txt = "0x400d1000: foo(int) at /a/b/src/x.cpp:12\n (inlined by) bar at /a/b/src/y.cpp:3\n0x400d2000: baz at ??:0\n"
    r = ht.parse_addr2line(txt)
    assert r[0x400D1000].startswith("foo(int) src/x.cpp:12")
    assert "bar" in r[0x400D1000] and 0x400D2000 in r


def test_site_location_skips_wrappers() -> None:
    s = ht.Site("1", [1, 2])
    assert ht.site_location(s, {1: "heap_caps_malloc heap_caps.c:1", 2: "loadCfg cfg.cpp:9"}) == "loadCfg cfg.cpp:9"


def test_logging_gap_breaks_line(tmp_path: Path) -> None:
    # Night 2026-11-22: the logger stopped 22:22-06:00; the chart drew that as data.
    log = tmp_path / "gap.log"
    log.write_text(
        "2026-11-22 22:00:00 [INSTR-HEAP] up_s 100 ifree 90000 imin 80000 iblk 60000\n"
        "2026-11-22 22:01:00 [INSTR-HEAP] up_s 160 ifree 91000 imin 80000 iblk 60000\n"
        "2026-11-23 06:00:00 [INSTR-HEAP] up_s 28900 ifree 95000 imin 80000 iblk 60000\n"
    )
    svg = ht.chart_svg(ht.parse_logs([log]), {})
    ifree_paths = [p for p in svg.split("<path ")[1:] if "var(--c-free)" in p]
    assert ifree_paths and ifree_paths[0].split('"')[1].count("M") == 2


def test_map_gap_kind_parsed() -> None:
    d = ht.parse_logs([SPACES])
    assert d.map is not None
