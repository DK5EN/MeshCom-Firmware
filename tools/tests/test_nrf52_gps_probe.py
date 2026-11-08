"""Source-pin tests for the nRF52 (RAK4631) L76K/UBLOX GPS detection in nrf52_main.cpp."""

import re
from pathlib import Path

SRC = Path(__file__).resolve().parents[2] / "src" / "nrf52" / "nrf52_main.cpp"
TEXT = SRC.read_text(encoding="utf-8", errors="replace")


def _between(start: str, end: str, text: str = TEXT) -> str:
    i = text.index(start)
    j = text.index(end, i)
    return text[i:j]


def _func(signature: str) -> str:
    """Body of a top-level function: from its definition to the next top-level '}' line."""
    m = re.search(r"^" + re.escape(signature) + r"\s*\{", TEXT, re.M)
    assert m, signature
    end = TEXT.index("\n}\n", m.end())
    return TEXT[m.start() : end]


def _baud_loop() -> str:
    i = TEXT.index("for(int iGpsBaud=0")
    j = TEXT.index("#endif", i)
    return TEXT[i:j]


def test_baud_loop_ends_serial_before_begin():
    loop = _baud_loop()
    k_end = loop.index("Serial1.end()")
    k_begin = loop.index("Serial1.begin(GPS_BAUDS[")
    assert k_end < k_begin


def test_fallback_runs_setupublox_when_no_probe_succeeded():
    region = _baud_loop()
    after = region[region.index("Serial1.begin(GPS_BAUDS[") :]
    assert "SetupUBLOX();" in after
    assert "UBLOX default setup" in after
    assert "Serial1.begin(9600)" in after


def test_settings_load_reads_module_choice_bits():
    block = _between("bUSER_BOARD_LED = meshcom_settings.node_sset3", "bDEBUGCSV")
    assert re.search(r"bGPSUBLOX\s*=\s*meshcom_settings\.node_sset3\s*&\s*0x2000", block)
    assert re.search(r"bGPSL76K\s*=\s*meshcom_settings\.node_sset3\s*&\s*0x4000", block)


def test_string_building_uses_append_and_reserve():
    for sig in ("String readUBX()", "String readUBXbin()", "bool GPSprobe()"):
        body = _func(sig)
        assert "ver.reserve(" in body, sig
        assert "ver = ver + char(" not in body, sig
        assert "ver += (char)" in body, sig
