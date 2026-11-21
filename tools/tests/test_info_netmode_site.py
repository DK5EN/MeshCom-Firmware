"""Structural pin for RAK-NETMODE-01 (#1289): the --info `NETWORK Mode:` line in
src/command_functions.cpp goes through infoNetModeLabel(), no node_netmode == 0 -> "WiFi" branch.

INFO_NETMODE_SRC_ROOT points the test at a mutated copy of src/ (mutation proof).
"""
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = Path(os.environ.get("INFO_NETMODE_SRC_ROOT", ROOT / "src"))


def _text():
    return (SRC / "command_functions.cpp").read_text(errors="replace")


def _block():
    t = _text()
    i = t.index('"...NETWORK Mode:')
    j = t.index("hasIpAddress", i)
    return t[i:j]


def test_block_calls_helper():
    b = _block()
    assert "infoNetModeLabel(" in b
    assert "NRF52_SERIES" in b and "HAS_NRF_ETH" in b


def test_no_wifi_branch_on_netmode_0():
    b = _block()
    assert "node_netmode == 0" not in b
    assert 'printlndeb("WiFi")' not in b


def test_header_included():
    assert '#include "netmode_label.h"' in _text()
