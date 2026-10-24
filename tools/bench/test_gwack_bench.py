"""Unit tests for the GWACK-01 bench driver's ACK pairing (no hardware)."""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import gwack_bench as g  # noqa: E402


def tx(aid: str, t: int, hop: str = "04") -> str:
    return f"10:00:00 10:00:00 [LOG] TX x{aid} A H{hop} prio=1 src=r wait=3000 q=2 cad=0 len=12 t={t}"


def rx(aid: str, hop: str, dup: str, target: str = "B5978333") -> str:
    return (f"10:00:00 10:00:00 [LOG] 012 A x{aid} H{hop} x{target} 01 00 RSSI:-50 SNR:6 "
            f"DUP:{dup} OWN:- t=1")


def test_own_ack_tx_skips_relays_and_texts():
    lines = [tx("0002286C", 100), tx("0002286C", 6600, hop="03"),
             "10:00:00 [LOG] TX xB5978333 : H04 prio=2 src=o wait=1 q=1 cad=0 len=46 t=5"]
    assert g.own_ack_tx(lines) == [("0002286C", 100)]


def test_pair_same_and_distinct_ids():
    own = [("0002286C", 100), ("0002286C", 6600), ("000409C4", 9000), ("000409C5", 14000)]
    rows = g.pair_acks(own)
    assert [r["kind"] for r in rows] == ["same", "distinct"]
    assert rows[0]["gap_ms"] == 6500
    assert rows[1]["id_delta"] == 1


def test_pair_does_not_join_two_texts_acks():
    # first copy of text 2 follows a lone copy of text 1: ids far apart -> two singles + a pair
    own = [("00037A24", 100), ("0003EDC5", 30000), ("0003EDC5", 42000)]
    assert [r["kind"] for r in g.pair_acks(own)] == ["single", "same"]


def test_pair_rejects_late_second_copy():
    own = [("00037A24", 0), ("00037A24", g.PAIR_MAX_GAP_MS + 1)]
    assert [r["kind"] for r in g.pair_acks(own)] == ["single", "single"]


def test_listener_tally_and_relay_per_pair():
    pairs = {"DK5EN-1": g.pair_acks([("0002286C", 0), ("0002286C", 6000)]),
             "DK5EN-92": g.pair_acks([("000409C4", 0), ("000409C5", 5000)])}
    ids = {"0002286C", "000409C4", "000409C5"}
    heard14 = g.heard_acks([rx("0002286C", "84", "n"), rx("0002286C", "84", "d"),
                            rx("0002286C", "83", "d"), rx("000409C4", "84", "n"),
                            rx("000409C5", "84", "n"), rx("000409C4", "83", "d"),
                            rx("000409C5", "83", "d")], ids)
    assert g.listener_tally(pairs, heard14, "DK5EN-14") == {"same:direct2:nd": 1,
                                                            "distinct:direct2:nn": 1}
    # a gateway's own pair does not count as heard by itself
    assert g.listener_tally(pairs, heard14, "DK5EN-1") == {"distinct:direct2:nn": 1}
    assert g.relay_per_pair(pairs, {"DK5EN-14": heard14}) == {"same:1": 1, "distinct:2": 1}
