"""Tests for tools/gw_legacy_capture.py (legacy gateway-server golden)."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("gw_legacy_capture", ROOT / "tools" / "gw_legacy_capture.py")
assert spec and spec.loader
cap = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cap)

COMMIT = cap.DEFAULT_COMMIT


@pytest.fixture(scope="module")
def sources() -> tuple[str, str]:
    return (
        cap.git_show(COMMIT, "src/udp_functions.cpp"),
        cap.git_show(COMMIT, "src/nrf52/nrf_eth.cpp"),
    )


@pytest.fixture(scope="module")
def generated() -> str:
    return cap.generate(COMMIT)


def _row(text: str, **kw: object) -> dict:
    hits = [r for r in json.loads(text)["rows"] if all(r[k] == v for k, v in kw.items())]
    assert len(hits) == 1, (kw, len(hits))
    return hits[0]


@pytest.mark.parametrize(
    ("which", "old", "new"),
    [
        (0, "const char *srv_host = NULL;", "const char *srv_host = nullptr;"),
        (0, "if(ntp_host == NULL)", "if(ntp_host == nullptr)"),
        (1, "const char *srv_path = NULL;   // TM-39", "const char *srv_path = NULL;"),
        (1, "#if INSTRUMENT_ENABLED", "#if INSTRUMENT_ENABLE_"),
        (1, 'const char *srv_path = "inet";', 'const char *srv_path = "net";'),
    ],
)
def test_extraction_fails_loudly_on_mutated_marker(sources: tuple[str, str], which: int, old: str, new: str) -> None:
    esp, nrf = sources
    assert old in sources[which]
    if which == 0:
        esp = esp.replace(old, new)
    else:
        nrf = nrf.replace(old, new)
    with pytest.raises(cap.ExtractionError):
        cap.extract_all(esp, nrf)


def test_extraction_fails_on_ambiguous_marker(sources: tuple[str, str]) -> None:
    esp, nrf = sources
    with pytest.raises(cap.ExtractionError):
        cap.extract_all(esp + "\n const char *srv_host = NULL;\n", nrf)


def test_cli_exits_nonzero_on_missing_marker(tmp_path: Path) -> None:
    golden = tmp_path / "g.json"
    golden.write_text("{}")
    # a commit without the old chains cannot satisfy the markers
    import subprocess
    import sys

    r = subprocess.run(
        [sys.executable, str(ROOT / "tools" / "gw_legacy_capture.py"), "--check", "--golden", str(golden), "--commit", "39642f0a"],
        capture_output=True,
        text=True,
        check=False,
    )
    assert r.returncode != 0


def test_deterministic(generated: str) -> None:
    assert cap.generate(COMMIT) == generated
    assert generated.endswith("\n")


def test_committed_golden_matches() -> None:
    committed = (ROOT / "test" / "golden" / "gw_servers_legacy.json").read_text()
    assert json.loads(committed)["header"]["commit"] == COMMIT
    assert cap.generate(COMMIT) == committed


def test_row_count(generated: str) -> None:
    assert len(json.loads(generated)["rows"]) == 7 * 5 * 4 * 3 + 2 * 7 * 5 * 4


def test_spot_checks(generated: str) -> None:
    r = _row(generated, platform="esp32", code="DL", hamnet_only=1, own_ip="44.143.1.5", own_ntp="")
    assert (r["srv"], r["path"]) == ("meshcom.hamnet.cloud", "hamnet")
    r = _row(generated, platform="esp32", code="HB", hamnet_only=0, own_ip="10.0.0.5", own_ntp="")
    assert r["srv"] == "meshcom.emcomm.ch"
    r = _row(generated, platform="esp32", code="OE", hamnet_only=0, own_ip="10.0.0.5", own_ntp="")
    assert r["srv"] == "meshcom.oevsv.at"
    r = _row(generated, platform="nrf_dyn", code="HC", hamnet_only=0, own_ip="10.0.0.5")
    assert r["path"] == "inet"
    r = _row(generated, platform="nrf_static", code="HB", hamnet_only=1, own_ip="10.0.0.5")
    assert (r["srv"], r["path"]) == ("44.143.8.143", "hamnet")


def test_nrf_dyn_hc_inet_uses_dl_internet_server(generated: str) -> None:
    # Legacy meaning: HC on the Internet uses the DL Internet server (192.68.17.26), path inet.
    r = _row(generated, platform="nrf_dyn", code="HC", hamnet_only=0, own_ip="10.0.0.5")
    assert (r["srv"], r["path"]) == ("192.68.17.26", "inet")


def test_esp32_44er_term_is_dead(generated: str) -> None:
    for code in ("OE", "DL", "HB"):
        r = _row(generated, platform="esp32", code=code, hamnet_only=0, own_ip="44.143.1.5", own_ntp="")
        assert r["path"] == "inet"


def test_nrf_ignores_own_ntp_and_static_44er_forces_hamnet(generated: str) -> None:
    rows = json.loads(generated)["rows"]
    assert {r["own_ntp"] for r in rows if r["platform"] != "esp32"} == {"n/a"}
    r = _row(generated, platform="nrf_static", code="OE", hamnet_only=0, own_ip="44.143.1.5")
    assert r["path"] == "hamnet"
