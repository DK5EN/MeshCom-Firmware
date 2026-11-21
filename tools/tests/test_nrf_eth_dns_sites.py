"""Structural pin for RAK-ETH-DNS-01 (#1289): both static-IP blocks of src/nrf52/nrf_eth.cpp
(initethfixIP, nrfStaticReapplyNoReset) take the DNS from ethStaticDns(), no hard-coded DNS literal.

NRF_ETH_SRC_ROOT points the test at a mutated copy of src/ (mutation proof).
"""
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = Path(os.environ.get("NRF_ETH_SRC_ROOT", ROOT / "src"))


def _text():
    return (SRC / "nrf52" / "nrf_eth.cpp").read_text(errors="replace")


def test_both_static_blocks_call_helper():
    t = _text()
    assert t.count("ethStaticDns(") == 2
    assert t.count("ethStaticDns(meshcom_settings.node_owndns, meshcom_settings.node_ownip)") == 2


def test_no_hardcoded_dns_literals():
    t = _text()
    assert '"8.8.8.8"' not in t
    assert '"44.143.0.10"' not in t


def test_header_included():
    assert '#include "eth_dns.h"' in _text()


def test_helper_header_reuses_parser():
    h = (SRC / "eth_dns.h").read_text()
    assert "extudp_parse_ipv4" in h and '#include "extudp_target.h"' in h
