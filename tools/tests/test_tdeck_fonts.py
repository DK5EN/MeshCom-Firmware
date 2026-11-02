"""CHARS-01 part B: the T-Deck Latin fonts must be drawable by the LVGL config they ship with.

LVGL 8.3 skips every glyph of a sub-pixel font (`.subpx != LV_FONT_SUBPX_NONE`) when
`LV_USE_FONT_SUBPX` is 0 (lib/lvgl/src/draw/sw/lv_draw_sw_letter.c, draw_letter): the letters
render as blank gaps. mc_font_latin_12 was first generated with `lv_font_conv --lcd` and hit
exactly that. Each font must also fall back to the built-in Montserrat of the same size, which
serves ASCII and LV_SYMBOL_*.
"""

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FONT_DIR = ROOT / "src" / "t-deck" / "fonts"
LV_CONFS = [ROOT / "variants" / v / "lv_conf.h" for v in ("t_deck", "t_deck_plus")]


def _fonts() -> list[Path]:
    fonts = sorted(FONT_DIR.glob("mc_font_latin_*.c"))
    assert fonts, f"no fonts in {FONT_DIR}"
    return fonts


def _subpx_enabled(conf: Path) -> bool:
    m = re.search(r"^#define LV_USE_FONT_SUBPX\s+(\d+)", conf.read_text(), re.M)
    assert m, f"LV_USE_FONT_SUBPX not set in {conf}"
    return m.group(1) != "0"


def test_no_subpixel_font_without_subpx_support() -> None:
    if all(_subpx_enabled(c) for c in LV_CONFS):
        return
    bad = [f.name for f in _fonts() if "LV_FONT_SUBPX_NONE" not in f.read_text()]
    assert not bad, f"sub-pixel fonts with LV_USE_FONT_SUBPX 0 render blank: {bad}"


def test_each_font_falls_back_to_same_size_montserrat() -> None:
    for f in _fonts():
        size = re.search(r"mc_font_latin_(\d+)\.c$", f.name).group(1)
        assert f".fallback = &lv_font_montserrat_{size}," in f.read_text(), f.name


REQUIRED = [
    0x00E4, 0x00DF, 0x00E9, 0x00E7, 0x00F1, 0x00E3, 0x00F5, 0x00BF, 0x00AB,  # DE FR ES PT
    0x0105, 0x0142, 0x017C, 0x0153, 0x0178,  # PL, French oe / Y diaeresis (Latin Ext-A)
    0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x201E, 0x2022, 0x2026, 0x20AC,  # phone punctuation
]  # fmt: skip


def test_every_font_carries_the_required_glyphs() -> None:
    for f in _fonts():
        text = f.read_text()
        missing = [f"U+{cp:04X}" for cp in REQUIRED if f"/* U+{cp:04X} " not in text]
        assert not missing, f"{f.name} lacks {missing}"


def test_lv_conf_declares_every_font() -> None:
    names = [f.stem for f in _fonts()]
    for conf in LV_CONFS:
        text = conf.read_text()
        for n in names:
            assert f"LV_FONT_DECLARE({n})" in text, f"{conf.parent.name}: {n} not declared"


COVER_H = ROOT / "src" / "t-deck" / "tdeck_text_cover.h"


def _header_cover_set() -> set[int]:
    """Codepoints >= 0x80 that tdeck_font_covers() in tdeck_text_cover.h accepts."""
    text = COVER_H.read_text(encoding="utf-8")
    m = re.search(r"static inline bool tdeck_font_covers\(uint32_t cp\)\s*\{(.*?)\n\}", text, re.S)
    assert m, "tdeck_font_covers() not found"
    body = m.group(1)
    cps: set[int] = set()
    for lo, hi in re.findall(r"cp >= (0x[0-9A-Fa-f]+) && cp <= (0x[0-9A-Fa-f]+)", body):
        cps.update(range(int(lo, 16), int(hi, 16) + 1))
    for c in re.findall(r"case (0x[0-9A-Fa-f]+):", body):
        cps.add(int(c, 16))
    return {c for c in cps if c >= 0x80}  # ASCII and LF come from the Montserrat fallback


def _font_cmap_set(font: Path) -> set[int]:
    """Codepoints in the cmaps of one lv_font_conv output file."""
    text = font.read_text()
    lists = {
        name: [int(v, 16) for v in re.findall(r"0x[0-9a-fA-F]+", body)]
        for name, body in re.findall(r"static const uint16_t (unicode_list_\d+)\[\]\s*=\s*\{(.*?)\};", text, re.S)
    }
    block = re.search(r"cmaps\[\]\s*=\s*\{(.*?)\n\};", text, re.S)
    assert block, f"{font.name}: no cmaps[]"
    cps: set[int] = set()
    entries = re.findall(r"\{\s*(\.range_start.*?)\}", block.group(1), re.S)
    assert entries, f"{font.name}: no cmap entries"
    for e in entries:
        start = int(re.search(r"\.range_start = (\d+)", e).group(1))
        length = int(re.search(r"\.range_length = (\d+)", e).group(1))
        kind = re.search(r"\.type = LV_FONT_FMT_TXT_CMAP_(\w+)", e).group(1)
        if kind.startswith("FORMAT0"):
            cps.update(range(start, start + length))
        elif kind.startswith("SPARSE"):
            name = re.search(r"\.unicode_list = (\w+)", e).group(1)
            n = int(re.search(r"\.list_length = (\d+)", e).group(1))
            assert len(lists[name]) == n, f"{font.name}: {name} length"
            cps.update(start + off for off in lists[name])
        else:
            raise AssertionError(f"{font.name}: unhandled cmap type {kind}")
    return cps


def test_cover_list_matches_every_font_cmap() -> None:
    covered = _header_cover_set()
    assert len(covered) > 200, "parser found suspiciously few codepoints"
    for f in _fonts():
        cmap = _font_cmap_set(f)
        assert cmap == covered, (
            f"{f.name} vs tdeck_text_cover.h: only in font "
            f"{sorted(f'U+{c:04X}' for c in cmap - covered)}, only in header "
            f"{sorted(f'U+{c:04X}' for c in covered - cmap)}"
        )
