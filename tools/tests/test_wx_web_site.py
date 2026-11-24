"""Structural pin for the WX table of the web UI (sub_page_wx in src/web_functions/web_functions.cpp).

The table listed only BME(P)280, BME680, MCU811, LPS33 and 1-Wire. A node with only an AHT20
showed every sensor "off" next to live Temperature/Humidity values (telegram P-0394). Every I2C
sensor that --info reports must have a row, guarded by its ENABLE_ define and fed by its on and
found flags, and the two temperature rows name their source (I2C, 1-Wire).

WX_WEB_SRC_ROOT points the test at a mutated copy of src/ (mutation proof).
"""

import os
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = Path(os.environ.get("WX_WEB_SRC_ROOT", ROOT / "src"))


def _body() -> str:
    t = (SRC / "web_functions" / "web_functions.cpp").read_text(errors="replace")
    i = t.index("void sub_page_wx()")
    j = t.index("\n}\n", i)
    return t[i:j]


def _rows(body: str) -> dict[str, tuple[str, str, str | None]]:
    """label -> (on flag, found flag, enclosing ENABLE_ define or None)."""
    rows: dict[str, tuple[str, str, str | None]] = {}
    guard: str | None = None
    for line in body.splitlines():
        s = line.strip()
        m = re.match(r"#if defined\s*\(\s*(ENABLE_\w+)\s*\)", s)
        if m:
            guard = m.group(1)
            continue
        if s.startswith("#endif"):
            guard = None
            continue
        m = re.match(r'_wx_sensor_row\("([^"]+)",\s*(\w+),\s*(\w+)\);', s)
        if m:
            rows[m.group(1)] = (m.group(2), m.group(3), guard)
    return rows


def test_every_i2c_sensor_has_a_row() -> None:
    rows = _rows(_body())
    assert rows == {
        "BMP390": ("bBMP3ON", "bmp3_found", "ENABLE_BMP390"),
        "AHT20": ("bAHT20ON", "aht20_found", "ENABLE_AHT20"),
        "SHT21": ("bSHT21ON", "sht21_found", "ENABLE_SHT21"),
        "INA226": ("bINA226ON", "ina226_found", "ENABLE_INA226"),
    }


def test_temperature_rows_name_their_source() -> None:
    body = _body()
    assert "<tr><td>Temperature (I2C)</td>" in body
    assert "<tr><td>Tout (1-Wire)</td>" in body
