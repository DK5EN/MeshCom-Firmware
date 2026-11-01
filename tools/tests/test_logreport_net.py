"""Regression test for the network-level 24-h log report topics (timesignal, manipulations, health).

Runs `tools/logreport/<topic>/run.py` on tiny fixture logs cut from the 2026-10-31 captures:
three logs with the logger wall-clock prefix (OE3XHL/OE3XIA/OE3XWJ, sharing the {CET} telegrams
x6AE58945..47 and some msg_ids) plus one standalone PuTTY-style log without the prefix (DB0AIS).
The expected numbers are exact counts of those fixtures.

Run: uv run --quiet --with pytest pytest -q tools/tests/test_logreport_net.py
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
LOGREPORT = ROOT / "tools" / "logreport"
FIXTURES = Path(__file__).resolve().parent / "fixtures" / "logreport" / "net"
CLUSTER = ["oe3xhl", "oe3xia", "oe3xwj"]


def run_topic(topic: str, norm: Path, out: Path, *extra: str) -> dict:
    env = {**os.environ, "TZ": "UTC"}
    proc = subprocess.run(
        [sys.executable, "-I", str(LOGREPORT / topic / "run.py"), "--norm", str(norm), "--out", str(out), *extra],
        capture_output=True, text=True, env=env, cwd=out.parent, timeout=300,
    )
    assert proc.returncode == 0, proc.stderr[-2000:]
    return json.loads((out / "results.json").read_text())


@pytest.fixture(scope="module")
def results(tmp_path_factory: pytest.TempPathFactory) -> dict[str, dict]:
    base = tmp_path_factory.mktemp("logreport")
    return {t: run_topic(t, FIXTURES, base / t) for t in ("timesignal", "manipulations", "health")}


def test_fixtures_are_small_and_clean() -> None:
    logs = sorted(FIXTURES.glob("*.log"))
    assert [p.stem for p in logs] == ["db0ais", *CLUSTER]
    assert sum(p.stat().st_size for p in logs) < 200_000
    for p in logs:
        low = p.read_bytes().lower()
        for banned in (b"regist", b"block", b"passw"):
            assert banned not in low, (p.name, banned)


def test_timesignal(results: dict[str, dict]) -> None:
    r = results["timesignal"]
    assert list(r["nodes"]) == [*CLUSTER, "db0ais"]  # cluster first, standalone last
    net = r["network"]
    assert net["distinct_ids"] == 3
    assert net["sources"] == {"OE1XAR-33": 3}
    assert net["db0ais_is_separate_mesh"] is True
    got = {n: (r["nodes"][n]["expected_in_window"], r["nodes"][n]["udp_got"], r["nodes"][n]["rf_valid_frames"])
           for n in CLUSTER}
    assert got == {"oe3xhl": (3, 3, 7), "oe3xia": (3, 3, 14), "oe3xwj": (3, 3, 4)}
    db = r["nodes"]["db0ais"]["by_source"]["OE1XAR-33"]
    assert (db["distinct_ids"], db["rf_frames"], db["own_tx"]) == (1, 2, 1)
    assert r["nodes"]["db0ais"]["rawpass"]["tx_total"] == 3


def test_manipulations(results: dict[str, dict]) -> None:
    r = results["manipulations"]
    assert list(r["nodes"]) == [*CLUSTER, "db0ais"]
    tot = {n: r["nodes"][n]["totals"] for n in r["nodes"]}
    assert tot["oe3xhl"]["rx_new"] == 2 and tot["oe3xhl"]["rx_dup_foreign"] == 7
    assert tot["oe3xia"]["rx_dup_foreign"] == 17 and tot["oe3xia"]["sym_ALT"] == 17
    assert tot["oe3xia"]["dropstale_ttl"] == 1
    assert tot["oe3xwj"]["tx_o_:"] == 2 and tot["oe3xwj"]["rx_dup_echo"] == 4
    assert tot["db0ais"]["rx_crc_err"] == 7 and tot["db0ais"]["gwu"] == 4
    assert r["network"]["redundancy_v1_nodes_per_id"] == {"1": 4, "2": 2, "3": 1}
    assert "cancel_eval_oe3xhl" in r["network"]  # no CANCEL line in the fixtures: first node, 0 cancels
    assert r["network"]["cancel_eval_oe3xhl"] == {"cancels": 0}


def test_health(results: dict[str, dict]) -> None:
    r = results["health"]
    assert list(r["nodes"]) == [*CLUSTER, "db0ais"]
    assert {n: r["nodes"][n]["has_wall_prefix"] for n in r["nodes"]} == {
        "oe3xhl": True, "oe3xia": True, "oe3xwj": True, "db0ais": False}
    assert r["nodes"]["db0ais"]["identity"]["callsign"] == "DB0AIS-12"
    st = r["nodes"]["oe3xwj"]["stat"]
    assert (st["rows"], st["up_first"], st["up_last"]) == (7, 300, 14402)
    assert r["nodes"]["oe3xia"]["time_signal"]["lora_cet_events"] == 14
    assert "eth04" in r["nodes"]["db0ais"]


def test_cluster_only_without_standalone(tmp_path: Path) -> None:
    """The node set is whatever *.log files are present: dropping the standalone log drops its sections."""
    norm = tmp_path / "norm"
    norm.mkdir()
    for n in CLUSTER:
        shutil.copy(FIXTURES / f"{n}.log", norm / f"{n}.log")
    ts = run_topic("timesignal", norm, tmp_path / "ts")
    assert list(ts["nodes"]) == CLUSTER
    assert "db0ais_is_separate_mesh" not in ts["network"]
    assert ts["network"]["distinct_ids"] == 3
    mp = run_topic("manipulations", norm, tmp_path / "mp")
    assert list(mp["nodes"]) == CLUSTER
    assert mp["nodes"]["oe3xia"]["totals"]["rx_dup_foreign"] == 17
