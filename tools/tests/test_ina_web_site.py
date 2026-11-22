"""Structural pin for the INA226 block of the web UI info page (src/web_functions/web_functions.cpp).

The vBUS label carried a stray '<' ("vBUS<: 12.21V"), which the browser shows literally or starts
parsing as a tag. Units must match the firmware values: node_vbus V, node_vshunt mV,
node_vcurrent mA, node_vpower mW (P-0391).

INA_WEB_SRC_ROOT points the test at a mutated copy of src/ (mutation proof).
"""

import os
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = Path(os.environ.get("INA_WEB_SRC_ROOT", ROOT / "src"))


def _block() -> str:
    t = (SRC / "web_functions" / "web_functions.cpp").read_text(errors="replace")
    i = t.index("<tr><td>INA226</td><td>")
    j = t.index("</td></tr>", i)
    return t[i:j]


def _formats(block: str) -> list[str]:
    return re.findall(r'web_client\.printf\("([^"]*)"', block)


def test_no_stray_angle_bracket_in_labels() -> None:
    for fmt in _formats(_block()):
        label = fmt.split(":", 1)[0]
        assert "<" not in label, f"stray '<' in INA226 label: {fmt!r}"


def test_units_match_firmware_values() -> None:
    fmts = _formats(_block())
    assert fmts == [
        "vBUS: %.2fV<br>",
        "vSHUNT: %.2fmV<br>",
        "vCURRENT: %.1fmA<br>",
        "vPOWER: %.1fmW<br>",
    ]
