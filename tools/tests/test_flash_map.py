"""Tests for tools/flash_map.py against a small synthetic ld map."""

from __future__ import annotations

import copy
import json
import subprocess
import sys
from pathlib import Path

import pytest

TOOLS = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(TOOLS))

import flash_map as fm  # noqa: E402

FIXTURE = Path(__file__).parent / "fixtures" / "flash_map_synthetic.map"


@pytest.fixture(scope="module")
def rep() -> dict:
    return fm.build_report(FIXTURE, label="demo", use_tools=False)


def test_sections_and_total(rep: dict) -> None:
    assert rep["chip"] == "esp32s3"
    assert rep["sections"] == {
        ".flash.text": 2592, ".flash.rodata": 608, ".iram0.text": 256, ".dram0.data": 48,
    }
    assert rep["image_bytes"] == 3504


def test_ram_and_noload_sections_are_excluded(rep: dict) -> None:
    for name in (".dram0.bss", ".flash.rodata_noload", ".xt.prop", ".flash_rodata_dummy"):
        assert name not in rep["sections"]


def test_groups(rep: dict) -> None:
    got = {k: v["bytes"] for k, v in rep["groups"].items()}
    assert got == {
        fm.G_WIFI: 1024,
        fm.G_SRC: 864,
        fm.G_TLS: 576,
        fm.G_RUNTIME: 384,
        fm.G_SYSTEM: 256,
        "lib: LVGL core": 208,
        "Arduino lib: Wire": 96,
        fm.G_ARDUINO: 64,
        fm.G_PAD: 32,
    }
    assert sum(got.values()) == rep["image_bytes"]


def test_merged_strings_counted_once(rep: dict) -> None:
    page = next(o for o in rep["objects"] if o["object"] == "src/web/page.cpp")
    # 0x80 text + 0x100 html + 0x20 new strings; the 0x40 duplicate string block adds nothing.
    assert page["bytes"] == 128 + 256 + 32


def test_archive_with_space_and_project_files(rep: dict) -> None:
    names = {a["archive"]: a for a in rep["archives"]}
    assert names["libLVGL core.a"]["group"] == "lib: LVGL core"
    assert names["src/main.cpp"]["group"] == fm.G_SRC
    assert [o["object"] for o in rep["src_files"]] == ["src/main.cpp", "src/web/page.cpp"]
    assert rep["src_bytes"] == 864


def test_symbols_merge_literal_with_text(rep: dict) -> None:
    sym = {s["name"]: s["bytes"] for s in rep["symbols"]}
    assert sym["_Z8mainloopv"] == 16 + 256
    assert sym["_ZL4html"] == 256
    assert sym["<string literals> src/main.cpp"] == 128
    assert sym["<.iram1.2> gpio.c.obj"] == 192


def test_ordering_is_descending(rep: dict) -> None:
    for key in ("archives", "objects", "symbols"):
        sizes = [r["bytes"] for r in rep[key]]
        assert sizes == sorted(sizes, reverse=True)


def test_unattributed_gap_goes_to_padding(tmp_path: Path) -> None:
    text = FIXTURE.read_text().replace(".dram0.data     0x000000003fc98000       0x30",
                                       ".dram0.data     0x000000003fc98000       0x40")
    m = tmp_path / "firmware.map"
    m.write_text(text)
    r = fm.build_report(m, use_tools=False)
    assert r["sections"][".dram0.data"] == 0x40
    assert r["groups"][fm.G_PAD]["bytes"] == 32 + 0x10


def test_classify_paths() -> None:
    sdk = "/h/.platformio/packages/framework-arduinoespressif32/tools/sdk/esp32/lib/"
    assert fm.classify(sdk + "liblwip.a(tcp.c.obj)")[0] == fm.G_LWIP
    assert fm.classify(sdk + "libmbedcrypto.a(aes.c.obj)")[0] == fm.G_TLS
    assert fm.classify(sdk + "libbt.a(x.o)")[0] == fm.G_BLE
    assert fm.classify(sdk + "libpp.a(pp.o)")[0] == fm.G_WIFI
    assert fm.classify(sdk + "libspi_flash.a(a.o)")[0] == fm.G_SYSTEM
    assert fm.classify(".pio/build/x/lib0a4/libU8g2.a(u8x8.c.o)")[0] == "lib: U8g2"
    assert fm.classify(".pio/build/x/lib75e/libWiFi.a(WiFi.cpp.o)")[0] == fm.G_WIFI
    assert fm.classify(".pio/build/x/lib75f/libWire.a(Wire.cpp.o)")[0] == "Arduino lib: Wire"
    assert fm.classify(".pio/build/x/src/esp32/esp32_main.cpp.o") == (
        fm.G_SRC, "src/esp32/esp32_main.cpp", "src/esp32/esp32_main.cpp")
    assert fm.classify("/t/toolchain-xtensa-esp32/x/libgcc.a(_divdi3.o)")[0] == fm.G_RUNTIME


def test_symbol_of() -> None:
    assert fm.symbol_of(".text._Z3foov", "a.o") == "_Z3foov"
    assert fm.symbol_of(".literal._Z3foov", "a.o") == "_Z3foov"
    assert fm.symbol_of(".rodata.str1.4", "a.o") == "<string literals> a.o"
    assert fm.symbol_of(".rodata.cst8", "a.o") == "<constants> a.o"
    assert fm.symbol_of(".text", "a.o") == "<.text> a.o"
    assert fm.symbol_of(".rodata._Z3foov.str1.1", "a.o") == "_Z3foov [strings]"


def test_diff_reports(rep: dict) -> None:
    new = copy.deepcopy(rep)
    new["image_bytes"] += 100
    new["groups"][fm.G_TLS]["bytes"] += 100
    out = "\n".join(fm.diff_reports(rep, new))
    assert "+100" in out and fm.G_TLS in out


def test_cli_json_and_html(tmp_path: Path) -> None:
    js = tmp_path / "r.json"
    cp = subprocess.run([sys.executable, str(TOOLS / "flash_map.py"), str(FIXTURE), "--no-tools",
                         "--label", "demo", "--json", str(js)], capture_output=True, text=True)
    assert cp.returncode == 0, cp.stderr
    data = json.loads(js.read_text())
    assert data["image_bytes"] == 3504 and data["label"] == "demo"
    out = tmp_path / "r.html"
    cp = subprocess.run([sys.executable, str(TOOLS / "flash_map.py"), "html", str(js), "-o",
                         str(out)], capture_output=True, text=True)
    assert cp.returncode == 0, cp.stderr
    page = out.read_text()
    assert "<svg" in page and "TLS/crypto" in page and "prefers-color-scheme" in page


def test_squarify_covers_area() -> None:
    rects = fm.squarify([("a", 6), ("b", 3), ("c", 1)], 0, 0, 100, 50)
    assert sum(w * h for _, _, _, w, h in rects) == pytest.approx(5000)


def test_help() -> None:
    cp = subprocess.run([sys.executable, str(TOOLS / "flash_map.py"), "--help"],
                        capture_output=True, text=True)
    assert cp.returncode == 0 and "firmware.map" in cp.stdout
