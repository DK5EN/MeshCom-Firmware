"""ETH-06: tools/w5100s_patch.py bounds the W5100S socketSendUDP() wait, and the firmware
drops the EXTUDP socket before every W5100S hardware reset (DB0AIS-12, 2026-11-14)."""

import configparser
import importlib.util
import re
from pathlib import Path

import pytest

TOOLS = Path(__file__).resolve().parents[1]
REPO = TOOLS.parent
FIXTURE = TOOLS / "tests/fixtures/w5100s_socket_send_udp.cpp"


def _load():
    spec = importlib.util.spec_from_file_location("w5100s_patch", TOOLS / "w5100s_patch.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def _fixture(eol: str) -> str:
    # library ships CRLF; git may hand the fixture out either way
    return FIXTURE.read_bytes().decode().replace("\r\n", "\n").replace("\n", eol)


def _wait_loop(text: str) -> str:
    start = text.index("while ( (W5100.readSnIR(s) & SnIR::SEND_OK)")
    return text[start : text.index("yield();", start)]


@pytest.mark.parametrize("eol", ["\r\n", "\n"])
def test_original_gets_a_deadline(eol):
    wp = _load()
    src = _fixture(eol)
    assert "millis()" not in src  # the bug: the loop has no bound of its own
    out = wp.patch_text(src)
    loop = _wait_loop(out)
    assert "millis() - meshcom_send_t0" in loop
    assert f">= {wp.DEADLINE_MS}" in loop
    # deadline branch clears the flags, ends the SPI transaction and fails the send
    branch = loop[loop.index("meshcom_send_t0) >=") : loop.index("if (W5100.readSnIR(s) & SnIR::TIMEOUT)")]
    assert "writeSnIR(s, (SnIR::SEND_OK|SnIR::TIMEOUT))" in branch
    assert "SPI.endTransaction();" in branch
    assert "return false;" in branch
    # start time taken before the loop, once
    assert out.index("uint32_t meshcom_send_t0 = millis();") < out.index("while ( (W5100.readSnIR(s)")
    # line endings kept
    if eol == "\r\n":
        assert "\n" not in out.replace("\r\n", "")
    else:
        assert "\r" not in out


def test_deadline_is_above_the_chips_own_udp_timeout():
    # DB0AIS: [ETH];stall;udp_tx;ms;1618..1646 -- the chip's ARP timeout; the bound must never cut that
    assert _load().DEADLINE_MS >= 1646 + 1000


def test_patched_is_idempotent():
    wp = _load()
    once = wp.patch_text(_fixture("\r\n"))
    assert wp.patch_text(once) == once


def test_unknown_text_raises():
    wp = _load()
    with pytest.raises(RuntimeError):
        wp.patch_text("bool EthernetClass::socketSendUDP(uint8_t s) { return true; }\n")


def test_original_twice_raises():
    wp = _load()
    src = _fixture("\n")
    with pytest.raises(RuntimeError):
        wp.patch_text(src + src)


def test_patch_file_roundtrip(tmp_path):
    wp = _load()
    p = tmp_path / "socket.cpp"
    p.write_bytes(_fixture("\r\n").encode())
    assert wp.patch_file(p) is True
    assert wp.patch_file(p) is False
    assert wp.MARKER in p.read_bytes().decode()
    assert wp.patch_file(tmp_path / "missing.cpp") is False


def test_rel_path_and_lib_deps_detection():
    wp = _load()
    assert wp.REL == "RAK13800-W5100S/src/socket.cpp"
    assert wp.wants_w5100s(["https://github.com/icssw-org/RAK13800-W5100S#091234f", "SX126x-Arduino"])
    assert not wp.wants_w5100s("\nh2zero/NimBLE-Arduino@2.2.3\nRadioLib")


def test_pre_script_is_wired_for_every_env():
    cfg = configparser.ConfigParser(interpolation=None, strict=False)
    cfg.read(REPO / "platformio.ini")
    scripts = cfg.get("env", "extra_scripts")
    assert "pre:tools/w5100s_patch.py" in scripts


def _func_body(text: str, signature: str) -> str:
    start = text.index(signature)
    brace = text.index("{", start)
    depth = 0
    for i in range(brace, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[brace : i + 1]
    raise AssertionError(signature)


def test_firmware_drops_extudp_socket_before_the_chip_reset():
    # every W5100S hardware reset goes through initETH_HW(); the EXTUDP socket must be dropped before the reset pin
    eth = (REPO / "src/nrf52/nrf_eth.cpp").read_text()
    body = _func_body(eth, "void NrfETH::initETH_HW()")
    assert "externUdpChipReset();" in body
    assert body.index("externUdpChipReset();") < body.index("digitalWrite(WB_IO3, LOW)")


def test_firmware_reopens_after_the_chip_reset():
    ext = (REPO / "src/extudp_functions.cpp").read_text()
    body = _func_body(ext, "void getExternUDP()")
    gate = body.index("if(!hasExternIPaddress)\n    return;")
    assert "extudp_reopen_after_chip_reset(" in body[:gate]
    # the drop never touches the chip (it may be why the reset happens) and clears the latch
    drop = _func_body(ext, "void externUdpChipReset()")
    assert "extudp_on_chip_reset(hasExternIPaddress" in drop
    assert "hasExternIPaddress = false;" in drop
    assert not re.search(r"UdpExtern\.(stop|begin|beginPacket|endPacket|write)\(", drop)
