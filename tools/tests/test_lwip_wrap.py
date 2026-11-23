"""FORMAL-01 lwIP #2: tools/lwip_wrap.py appends the link-time wrap flags per framework line."""

import importlib.util
import json
from pathlib import Path

import pytest

TOOLS = Path(__file__).resolve().parents[1]


def _load():
    spec = importlib.util.spec_from_file_location("lwip_wrap", TOOLS / "lwip_wrap.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


LW = _load()


@pytest.mark.parametrize(
    "version,expected",
    [
        ("3.20017.241212+sha.dcc1105b", True),  # arduino-esp32 2.0.17 (espressif32@^6.6)
        ("3.20014.231204", True),  # arduino-esp32 2.0.14 (espressif32@6.5.0)
        ("3.20009.0", True),
        ("3.3.7+sha.b3b492ff", False),  # Tasmota / IDF 5 (safeboot)
        ("3.50000.0", False),  # a 5.0 core
        ("2.0.17", False),
        ("", False),
        ("garbage", False),
    ],
)
def test_framework_is_idf44_line(version, expected):
    assert LW.is_arduino_2_0_line(version) is expected


def test_framework_version_reads_package_json(tmp_path):
    (tmp_path / "package.json").write_text(json.dumps({"version": "3.20017.241212+sha.x"}))
    assert LW.framework_version(tmp_path) == "3.20017.241212+sha.x"


@pytest.mark.parametrize("root", [None, "missing"])
def test_framework_version_unreadable_is_empty(tmp_path, root):
    assert LW.framework_version(None if root is None else tmp_path / root) == ""


@pytest.mark.parametrize(
    "platform,frameworks,version,expected",
    [
        ("espressif32", ["arduino"], "3.20017.1", True),
        ("espressif32", "arduino", "3.20014.1", True),
        ("espressif32", ["arduino", "espidf"], "3.20017.1", True),
        ("espressif32", ["arduino"], "3.3.7+sha.b3b492ff", False),
        ("espressif32", ["espidf"], "3.20017.1", False),
        ("nordicnrf52", ["arduino"], "", False),
        ("native", [], "", False),
    ],
)
def test_wants_lwip_wrap(platform, frameworks, version, expected):
    assert LW.wants_lwip_wrap(platform, frameworks, version) is expected


class _Env:
    def __init__(self, linkflags=()):
        self.vars = {"LINKFLAGS": list(linkflags)}

    def get(self, key, default=None):
        return self.vars.get(key, default)

    def Append(self, **kw):
        for k, v in kw.items():
            self.vars.setdefault(k, []).extend(v)


def test_add_wrap_flags_appends_all_three_and_reports(capsys):
    env = _Env(["-Wl,--gc-sections"])
    added = LW.add_wrap_flags(env)
    assert added == list(LW.WRAP_FLAGS)
    assert env.vars["LINKFLAGS"][1:] == [
        "-Wl,--wrap=netif_set_addr",
        "-Wl,--wrap=ip4_frag",
        "-Wl,--wrap=tcp_eff_send_mss_netif",
    ]
    err = capsys.readouterr().err
    assert err.count("\n") == 1
    for flag in LW.WRAP_FLAGS:
        assert flag in err


def test_add_wrap_flags_skips_flags_already_present(capsys):
    env = _Env(["-Wl,--wrap=ip4_frag"])
    added = LW.add_wrap_flags(env)
    assert "-Wl,--wrap=ip4_frag" not in added
    assert env.vars["LINKFLAGS"].count("-Wl,--wrap=ip4_frag") == 1
    assert len(env.vars["LINKFLAGS"]) == 3
    assert LW.add_wrap_flags(env) == []
    assert capsys.readouterr().err.count("\n") == 1  # nothing printed the second time
