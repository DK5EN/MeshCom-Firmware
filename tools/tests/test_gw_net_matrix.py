"""Offline tests for tools/bench/gw_net_matrix.py (no hardware, no serial port)."""

from __future__ import annotations

import shutil
import sys
import tempfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "bench"))
import gw_net_matrix as g  # noqa: E402

GOOD = [
    "[BOOT];ready;ms;1234;ip;192.168.68.62;eth;1",
    "[GW];srv;DL;host;192.68.17.26;path;inet;ms;812",
    "[GW];keep;tx;ok;1;ms;40",
    "[GW];rx;type;BEAT;len;12",
    "[NTP];ok;epoch;1790000000;rtt;35",
]
EXP = {"srv": "192.68.17.26", "path": "inet"}


def obs(lines: list[str] = GOOD, mode: str = "auto", in_use: str = "inet") -> dict:
    o = g.parse_lines(lines)
    o["netmode"], o["in_use"] = mode, in_use
    return o


def test_parse_markers() -> None:
    o = g.parse_lines(GOOD + ["Network mode: auto (in use: inet)"])
    assert (o["srv"], o["host"], o["path"]) == ("DL", "192.68.17.26", "inet")
    assert o["keep"] and o["answer"] and o["ntp"]
    assert (o["netmode"], o["in_use"]) == ("auto", "inet")
    assert g.complete(o)
    assert g.parse_lines(["[GW];rx;type;CONF;x"])["answer"]
    assert g.parse_lines(["[NTP];timeout"])["ntp_fail"] == "timeout"
    assert not g.complete(g.parse_lines(GOOD[:3]))


def test_parse_info() -> None:
    info = g.parse_info(["...Call: <DK5EN-90>", "...FREQ 433.175 MHz TXPWR 2 dBm RXBOOST on",
                         "...IP address   : 192.168.68.62"])  # fmt: skip
    assert info == {"call": "DK5EN-90", "txpower": 2, "ip": "192.168.68.62"}


def test_judge_pass() -> None:
    assert g.judge("DL", "auto", EXP, obs(), "rak") == []


@pytest.mark.parametrize(
    "mutate,needle",
    [
        (lambda o: o.update(srv="OE"), "srv code"),
        (lambda o: o.update(path="hamnet"), "path"),
        (lambda o: o.update(host="1.2.3.4"), "host"),
        (lambda o: o.update(keep=False), "keep"),
        (lambda o: o.update(answer=False), "answer"),
        (lambda o: o.update(ntp=False), "NTP"),
        (lambda o: o.update(netmode="inet"), "netmode"),
        (lambda o: o.update(in_use="hamnet"), "in-use"),
    ],
)
def test_judge_fail(mutate, needle: str) -> None:  # noqa: ANN001
    o = obs()
    mutate(o)
    fails = g.judge("DL", "auto", EXP, o, "rak")
    assert any(needle in f for f in fails), fails


def test_judge_esp32_ip_for_hostname_ok() -> None:
    exp = {"srv": "meshcom.hamnet.network", "path": "inet"}
    o = obs()
    assert g.judge("DL", "auto", exp, o, "heltec") == []
    assert any("host" in f for f in g.judge("DL", "auto", exp, o, "rak"))


def test_whitelist() -> None:
    for ok in ["--info", "--gateway srv OE", "--setnet", "--setnet hamnet", "--reboot",
               "--setownip 0", "--setownms 255.255.252.0", "--debug on"]:  # fmt: skip
        assert g.check_cmd(ok) == ok
    for bad in ["::{9}hello", ":hello", "--txpower 20", "--sendmsg hi", "--gateway srv oe",
                "--setnet foo", "--info; --reboot"]:  # fmt: skip
        with pytest.raises(ValueError):
            g.check_cmd(bad)


@pytest.fixture(scope="module")
def oracle():  # noqa: ANN201
    if not (shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")):
        pytest.skip("no C++ compiler")
    return g.build_oracle(ROOT / "src", Path(tempfile.mkdtemp()))


def test_oracle_rows(oracle) -> None:  # noqa: ANN001
    ip = "192.168.68.62"
    r = g.run_oracle(oracle, [("esp32", "DL", ip, 3), ("nrf_static", "OE", ip, 3)])
    assert r[0]["srv"] == "meshcom.hamnet.network" and r[0]["path"] == "inet"
    assert r[1]["srv"] == "89.185.97.38" and r[1]["path"] == "inet"


def test_dry_run_cases(oracle) -> None:  # noqa: ANN001
    nets = g.make_nets(["N2"])
    cases = g.make_cases("rak", nets, ["hamnet"], ["OE"], {"N2": "192.168.68.62"}, oracle)
    assert cases[0].expected["path"] == "hamnet"
    assert cases[0].expected["srv"] == "44.143.8.143"


def test_window_starts_at_srv_line_before_boot_marker() -> None:
    """Real order (RAK and Heltec bench, 2026-10-16): [GW];srv comes before [BOOT];ready."""
    lines = [
        "[GW];keep;tx;ok;1;ms;102271",  # old boot, before the reboot took effect
        "[GW];rx;type;BEAT;len;20;ms;102489",
        "[GW];srv;OE;host;89.185.97.38;path;inet;ms;19106",
        "[GW];keep;tx;ok;1;ms;19107",
        "[BOOT];ready;ms;19110;ip;1;eth;1",
        "[GW];rx;type;BEAT;len;20;ms;19231",
        "[NTP];ok;epoch;1792130106;rtt;106",
    ]
    o = g.parse_lines(lines[g.last_boot_start(lines) :])
    assert (o["srv"], o["host"], o["path"]) == ("OE", "89.185.97.38", "inet")
    assert g.complete(o)


def test_judge_uses_firmware_mode_labels() -> None:
    o = obs(mode="Internet")
    assert g.judge("DL", "inet", EXP, o, "rak") == []
    assert any("netmode" in f for f in g.judge("DL", "inet", EXP, obs(mode="inet"), "rak"))


def test_in_use_is_selected_network_not_path() -> None:
    """IT with --setnet hamnet: path inet, but "in use" reports the selected network hamnet."""
    exp = {"srv": "meshcom.dig-italia.it", "path": "inet"}
    o = g.parse_lines(["[GW];srv;IT;host;meshcom.dig-italia.it;path;inet;ms;1"] + GOOD[2:])
    o["netmode"], o["in_use"] = "Hamnet", "hamnet"
    assert g.judge("IT", "hamnet", exp, o, "heltec") == []
    assert g.selected_net("auto", "44.143.1.2") == "hamnet"
    assert g.selected_net("auto", "192.168.68.71") == "inet"
