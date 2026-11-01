"""logreport txpool + decay topics on tiny real-line fixtures (two wall-clock nodes + one unclocked node)."""

from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))

from logreport.decay import run as decay_run  # noqa: E402
from logreport.txpool import run as txpool_run  # noqa: E402

FIX = Path(__file__).resolve().parent / "fixtures" / "logreport" / "txpool_decay"


@pytest.fixture(scope="module")
def txpool_res(tmp_path_factory: pytest.TempPathFactory) -> dict:
    out = tmp_path_factory.mktemp("txpool")
    assert txpool_run.main(["--norm", str(FIX), "--out", str(out)]) == 0
    res = json.loads((out / "results.json").read_text())
    res["_reads"] = {n: json.loads((out / f"{n}.reads.json").read_text()) for n in res["nodes"]}
    return res


@pytest.fixture(scope="module")
def decay_res(tmp_path_factory: pytest.TempPathFactory) -> dict:
    out = tmp_path_factory.mktemp("decay")
    assert decay_run.main(["--norm", str(FIX), "--out", str(out)]) == 0
    return json.loads((out / "results.json").read_text())


def test_node_set_comes_from_files(txpool_res: dict, decay_res: dict) -> None:
    assert sorted(txpool_res["nodes"]) == ["db0ais", "oe3xia", "oe3xoc"]
    # decay: only wall-clock nodes are analysed per node, the unclocked log is a network-level entry
    assert sorted(decay_res["nodes"]) == ["oe3xia", "oe3xoc"]
    assert "db0ais" in decay_res["network"]


def test_txpool_writes_and_fates(txpool_res: dict) -> None:
    n = txpool_res["nodes"]
    assert n["oe3xia"]["fate"]["total_writes"] == 12
    assert n["oe3xia"]["fate"]["counts"] == {"pending_end": 7, "tx_air": 4, "superseded": 1}
    assert n["oe3xoc"]["fate"]["total_writes"] == 12
    assert n["oe3xoc"]["fate"]["counts"] == {"stale_ttl": 1, "tx_air": 11}
    assert n["db0ais"]["fate"]["counts"] == {"tx_air": 3}
    assert txpool_res["network"]["oe3_total_writes"] == 24  # pooled over the wall-clock nodes only


def test_txpool_tx_read_counts(txpool_res: dict) -> None:
    # matched RING_TX_READ events; the fixture holds 3/6/12 raw markers, the rest are reads of
    # slots written before the cut (counted as unmatched_reads)
    reads = txpool_res["_reads"]
    assert {k: len(v) for k, v in reads.items()} == {"db0ais": 3, "oe3xia": 4, "oe3xoc": 11}
    assert txpool_res["nodes"]["oe3xoc"]["fate"]["unmatched_reads"] == 1


def test_txpool_fate_shares_sum_to_100(txpool_res: dict) -> None:
    for node in txpool_res["nodes"].values():
        assert node["fate"]["sum_pct"] == pytest.approx(100.0, abs=0.5)
        assert sum(node["fate"]["pct"].values()) == pytest.approx(100.0, abs=0.5)
    assert sum(txpool_res["network"]["oe3_fate_pct"].values()) == pytest.approx(100.0, abs=0.5)


def test_decay_ttl_drops(decay_res: dict) -> None:
    n = decay_res["nodes"]
    assert n["oe3xia"]["drops_total"] == 5
    assert n["oe3xia"]["reason"] == {"ttl": 5}
    assert n["oe3xia"]["by_prio_cat"] == {"p4/unknown_prelog": 2, "p5/unknown_prelog": 3}
    assert n["oe3xoc"]["drops_total"] == 1
    assert n["oe3xoc"]["by_prio_cat"] == {"p5/own_HEY": 1}
    assert decay_res["network"]["drops_total"] == 6


def test_decay_context_and_unclocked_node(decay_res: dict) -> None:
    n = decay_res["nodes"]
    assert (n["oe3xia"]["context"]["tx"], n["oe3xoc"]["context"]["tx"]) == (6, 12)
    prio3 = decay_res["network"]["db0ais"]["tx_by_prio"]["p3"]
    assert prio3["n"] == 3 and prio3["n_gt_60s"] == 0
    assert decay_res["network"]["db0ais"]["ring_drop_stale"] == 0
