"""logreport topics on a second mesh: two ESP32-S3 (Heltec V3) nodes, one with `--setlog on`, one without.

Fixtures are real lines cut from the 2026-10-31 HB9VQQ-4 / HB9VQQ-5 captures (tools/meshlogger.py over
TCP 2323, --loradebug): hb9vqq4 carries [LOG] lines, hb9vqq5 only loradebug (MH-LoRa / RX-LoRa / [MC-DBG]).
`ackgap/` is a 56 s slice of hb9vqq4 in which 12-byte ACK receptions (no OnRxDone line) fill an
OnRxDone gap of 49 s.

Run: uv run --quiet --with pytest pytest -q tools/tests/test_logreport_hb9.py
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
LOGREPORT = ROOT / "tools" / "logreport"
FIX = Path(__file__).resolve().parent / "fixtures" / "logreport" / "hb9"
OWN = ["--owncall", "hb9vqq4=HB9VQQ-4", "--owncall", "hb9vqq5=HB9VQQ-5"]
TOPICS = ("txpool", "decay", "timesignal", "manipulations", "health")


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
    base = tmp_path_factory.mktemp("hb9")
    return {t: run_topic(t, FIX, base / t, *(OWN if t in ("timesignal", "manipulations", "health") else ())) for t in TOPICS}


def test_fixtures_are_small_and_clean() -> None:
    logs = sorted(FIX.rglob("*.log"))
    assert len(logs) == 3
    assert sum(p.stat().st_size for p in logs) < 150_000
    for p in logs:
        low = p.read_bytes().lower()
        for banned in (b"regist", b"block", b"passw"):
            assert banned not in low, (p.name, banned)


def test_txpool_completes(results: dict[str, dict]) -> None:
    n = results["txpool"]["nodes"]
    assert sorted(n) == ["hb9vqq4", "hb9vqq5"]
    assert n["hb9vqq4"]["fate"]["counts"] == {"tx_air": 3}
    assert n["hb9vqq5"]["fate"]["total_writes"] == 3


def test_decay_skips_node_without_log_lines(results: dict[str, dict]) -> None:
    r = results["decay"]
    assert sorted(r["nodes"]) == ["hb9vqq4"]
    assert r["nodes"]["hb9vqq4"]["context"]["tx"] == 3
    assert list(r["unavailable"]) == ["hb9vqq5"]
    assert "--setlog on" in r["unavailable"]["hb9vqq5"]
    assert "hb9vqq5" in r["network"]  # ring counters still reported


def test_timesignal_two_node_group(results: dict[str, dict]) -> None:
    r = results["timesignal"]
    assert sorted(r["nodes"]) == ["hb9vqq4", "hb9vqq5"]
    assert r["network"]["nodes_without_log_lines"] == ["hb9vqq5"]
    assert r["nodes"]["hb9vqq4"]["rf_valid_frames"] == 8
    assert r["nodes"]["hb9vqq4"]["rawpass"]["tx_total"] == 3
    q = r["nodes"]["hb9vqq5"]
    assert q["cet_events_by_kind"]["cet"] == 3
    assert set(q["unavailable"]) >= {"rf_age_and_copies", "tx_airtime_wait_prio"}


def test_manipulations_two_node_group(results: dict[str, dict]) -> None:
    n = results["manipulations"]["nodes"]
    assert n["hb9vqq4"]["totals"]["rx_crc_err"] == 2
    assert n["hb9vqq4"]["totals"]["cadbusy"] == 19
    assert n["hb9vqq5"]["events_by_kind"]["ringwrite"] == 3
    assert "relay_fate_cancel_opportunity_ledger" in n["hb9vqq5"]["unavailable"]


def test_health_two_node_group(results: dict[str, dict]) -> None:
    n = results["health"]["nodes"]
    a, b = n["hb9vqq4"], n["hb9vqq5"]
    assert "unavailable" not in a
    assert set(b["unavailable"]) == {"traffic_rf_neighbours_firmware_census", "stat_windows_heap_ringmax_drops",
                                     "own_tx_prio_wait_src"}
    # ESP32: RX errors are CRC_ERROR lines, good receptions are counted per ONRXDONE_TIME
    assert (a["rf"]["rx_error_n"], a["rf"]["rx_good_events"]) == (2, 15)
    assert (b["rf"]["rx_error_n"], b["rf"]["rx_good_events"]) == (4, 7)
    assert a["rf"]["rx_error_share_of_rx_pct"] == 11.8


def test_health_counts_ack_receptions(tmp_path: Path) -> None:
    """12-byte ACK frames print no OnRxDone line. Old counting (OnRxDone + OnRxError): 4 RX events, longest
    silence 48.9 s, RX_ERROR share 25.0 %. Counting ONRXDONE_TIME (+ OnRxError): 19 events, 7.7 s, 5.3 %."""
    r = run_topic("health", FIX / "ackgap", tmp_path / "ack", "--owncall", "hb9vqq4=HB9VQQ-4")["nodes"]["hb9vqq4"]
    assert r["rx_silence"]["rx_events"] == 19
    assert r["rx_silence"]["longest_gaps"][0]["gap_s"] == pytest.approx(7.7, abs=0.1)
    assert r["rf"]["rx_good_events"] == 18
    assert r["rf"]["rx_error_share_of_rx_pct"] == 5.3
