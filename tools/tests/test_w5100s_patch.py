"""ETH-06/ETH-07: tools/w5100s_patch.py bounds the W5100S socketSendUDP() wait, and the firmware
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


def test_firmware_drops_applink_socket_before_the_chip_reset():
    # ETH-06 x AppLink: the reset runs inside one loop pass, so applinkLoop() never sees "no IP"; afterwards
    # s_sock may name a socket the chip handed to someone else (gateway UDP) -- AppLink must forget it first.
    eth = (REPO / "src/nrf52/nrf_eth.cpp").read_text()
    body = _func_body(eth, "void NrfETH::initETH_HW()")
    assert "applinkChipReset();" in body
    assert body.index("applinkChipReset();") < body.index("digitalWrite(WB_IO3, LOW)")


def test_applink_chip_reset_never_touches_the_chip():
    src = (REPO / "src/nrf52/applink_nrf52.cpp").read_text()
    body = _func_body(src, "void applinkChipReset()")
    assert not re.search(r"W5100\.|execCmdSn|SPI\.|EthernetClient|s_srv\.", body)
    # the index is forgotten before the session close, whose alIoClose -> alCloseSocket() would CLOSE it on the chip
    assert body.index("s_sock = 0xFF;") < body.index("alConnClose(")
    assert "s_staleSweep = false;" in body  # the reset closed every chip socket; nothing stale to sweep
    hdr = (REPO / "src/applink.h").read_text()
    assert "void applinkChipReset();" in hdr
    assert "static inline void applinkChipReset() {}" in hdr  # no-op where there is no W5100S (ESP32, T114)


def test_applink_checks_socket_ownership_before_chip_access():
    # Advisor finding 2026-11-21: a session socket that went CLOSED can be handed to the web server, the gateway UDP
    # socket, DHCP or NTP by Ethernet.socketBegin() (it zeroes EthernetServer::server_port[s]) while AppLink still
    # holds the number. Every chip access must first check the RAM claim server_port[s_sock] == AL_PORT.
    src = (REPO / "src/nrf52/applink_nrf52.cpp").read_text()
    assert "static bool alOwnsSock()" in src
    own = _func_body(src, "static bool alOwnsSock()")
    assert "EthernetServer::server_port[s_sock] == AL_PORT" in own
    close = _func_body(src, "static void alCloseSocket()")
    assert close.index("alOwnsSock()") < close.index("execCmdSn")
    # another user's claim is never cleared
    assert "EthernetServer::server_port[s_sock] = 0;" not in close.replace("if (owned)\n        EthernetServer::server_port[s_sock] = 0;", "")
    write = _func_body(src, "static int alIoWrite(")
    assert write.index("alOwnsSock()") < write.index("EthernetClient c(s_sock)")
    loop = _func_body(src, "void applinkLoop()")
    assert loop.index("alOwnsSock()") < loop.index("EthernetClient cl(s_sock)")
    reset = _func_body(src, "void applinkChipReset()")
    assert "alOwnsSock()" in reset


def test_applink_costs_no_static_ram_while_off():
    # T-Beam idle-heap gate 2026-11-21: AppLink held 5.4 kB of static RAM (connection state, receive buffer,
    # handshake scratch) on every node, also with AppLink off: idle heap 3,360 B below the baseline.
    # The memory now comes from the heap while AppLink is wanted, and only applinkLoop() frees it (a command
    # arriving over AppLink runs inside alConnRx and must never pull the memory out from under it).
    eng = (REPO / "src/applink_conn.cpp").read_text()
    assert not re.search(r"static\s+(uint8_t|char)\s+\w+\[", eng)
    for path, ensure, release in (("src/esp32/applink_esp32.cpp", "memEnsure()", "memRelease()"),
                                  ("src/nrf52/applink_nrf52.cpp", "alMemEnsure()", "alMemRelease()")):
        src = (REPO / path).read_text()
        assert not re.search(r"^static\s+AlConn\s", src, re.M), path
        assert not re.search(r"^static\s+uint8_t\s+\w+\[\s*(AL_RX_CHUNK|APPLINK_OUT_BUF|APPLINK_HTTP_MAX)", src, re.M), path
        assert "static AlMem *s_mem = nullptr;" in src
        loop = _func_body(src, "void applinkLoop()")
        assert ensure in loop and release in loop, path
        # allocation and release appear nowhere else than their own definitions and applinkLoop()
        rest = re.sub(r"//[^\n]*", "", src.replace(loop, ""))  # names in comments do not count
        rest = re.sub(r"static bool " + re.escape(ensure) + r"\s*\{.*?\n\}", "", rest, flags=re.S)
        rest = re.sub(r"static void " + re.escape(release) + r"\s*\{.*?\n\}", "", rest, flags=re.S)
        assert ensure not in rest and release not in rest, path
        # exactly one free, and it sits inside the release function (a direct free elsewhere is the
        # nested-command use-after-free this test exists for; advisor mutation 2026-11-21)
        assert src.count("free(s_mem)") == 1 and src.count("calloc(1, sizeof(AlMem))") == 1, path
        assert "free(s_mem)" in _func_body(src, "static void " + release), path


# ---- ETH-07: socket index bounds guards (RAKWireless ebea549) ----


def _pristine07(wp, name: str, eol: str) -> str:
    """Synthetic unpatched library file: every anchor followed by a body line, as in the library."""
    parts = [f"{sig}{eol}{{{eol}\tbody();{eol}}}{eol}{eol}" for sig, _ in wp.ETH07[name]]
    return "".join(parts)


ETH07_FILES = ["socket.cpp", "EthernetUdp.cpp", "EthernetClient.cpp"]


@pytest.mark.parametrize("eol", ["\r\n", "\n"])
@pytest.mark.parametrize("name", ETH07_FILES)
def test_eth07_applies_to_pristine_text(name, eol):
    wp = _load()
    src = _pristine07(wp, name, eol)
    assert wp.MARKER07 not in src
    out = wp.patch_text07(name, src)
    assert out.count(wp.MARKER07) == len(wp.ETH07[name])
    for sig, guard in wp.ETH07[name]:
        # guard is the first statement of its function
        assert f"{sig}{eol}{{{eol}\t{guard} {wp.MARKER07}{eol}\tbody();" in out
    assert out.count("MAX_SOCK_NUM") == len(wp.ETH07[name])
    if eol == "\r\n":
        assert "\n" not in out.replace("\r\n", "")
    else:
        assert "\r" not in out


def test_eth07_covers_all_upstream_entry_points():
    wp = _load()
    assert len(wp.ETH07["socket.cpp"]) == 13
    assert len(wp.ETH07["EthernetUdp.cpp"]) == 6
    assert len(wp.ETH07["EthernetClient.cpp"]) == 1
    # a failed allocation must not reach the chip: return values are the neutral ones
    guards = dict(wp.ETH07["socket.cpp"])
    assert guards["bool EthernetClass::socketSendUDP(uint8_t s)"].endswith("false;")
    assert guards["void EthernetClass::socketClose(uint8_t s)"].endswith("return;")


@pytest.mark.parametrize("name", ETH07_FILES)
def test_eth07_is_idempotent(name):
    wp = _load()
    once = wp.patch_text07(name, _pristine07(wp, name, "\r\n"))
    assert wp.patch_text07(name, once) == once


@pytest.mark.parametrize("name", ETH07_FILES)
def test_eth07_raises_on_changed_text(name):
    wp = _load()
    src = _pristine07(wp, name, "\n")
    sig = wp.ETH07[name][0][0]
    with pytest.raises(RuntimeError, match="ETH-07"):
        wp.patch_text07(name, src.replace(sig, sig.replace("(", " (", 1)))  # signature drifted
    with pytest.raises(RuntimeError, match="ETH-07"):
        wp.patch_text07(name, src + src)  # anchor twice


def test_eth07_completes_a_partly_patched_file():
    wp = _load()
    name = "EthernetUdp.cpp"
    src = _pristine07(wp, name, "\n")
    orig, patched = wp.eth07_edits(name)[0]
    part = src.replace(orig, patched)
    assert part.count(wp.MARKER07) == 1
    assert wp.patch_text07(name, part) == wp.patch_text07(name, src)


@pytest.mark.parametrize("eol", ["\r\n", "\n"])
def test_eth06_and_eth07_compose_in_either_order(eol):
    wp = _load()
    src = _fixture(eol)  # holds the real socketSendUDP() text
    # fixture lacks the other socket*() anchors: add them around it
    full = _pristine07(wp, "socket.cpp", eol).replace(
        f"bool EthernetClass::socketSendUDP(uint8_t s){eol}{{{eol}\tbody();{eol}}}{eol}{eol}", ""
    ) + src
    six_first = wp.patch_text07("socket.cpp", wp.patch_text(full))
    seven_first = wp.patch_text(wp.patch_text07("socket.cpp", full))
    assert six_first == seven_first
    assert wp.MARKER in six_first and wp.MARKER07 in six_first
    body = _func_body(six_first.replace("\r\n", "\n"), "bool EthernetClass::socketSendUDP(uint8_t s)")
    # ETH-07 guard first, then the SPI transaction, then the ETH-06 deadline
    assert body.index("MAX_SOCK_NUM") < body.index("beginTransaction") < body.index("meshcom_send_t0")
    assert wp.patch_text07("socket.cpp", wp.patch_text(six_first)) == six_first


def test_eth07_patch_file_roundtrip(tmp_path):
    wp = _load()
    p = tmp_path / "EthernetClient.cpp"
    p.write_bytes(_pristine07(wp, "EthernetClient.cpp", "\r\n").encode())
    fn = lambda t: wp.patch_text07("EthernetClient.cpp", t)  # noqa: E731
    assert wp.patch_file(p, fn) is True
    assert wp.patch_file(p, fn) is False
    assert wp.MARKER07 in p.read_bytes().decode()
