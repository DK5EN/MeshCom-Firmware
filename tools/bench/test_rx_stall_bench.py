"""Offline tests for rx_stall_bench.py (fixtures only, no network, no hardware)."""

from __future__ import annotations

import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import rx_stall_bench as rb  # noqa: E402

FIX = HERE / "fixtures" / "rx_stall"


def lines(name: str) -> list[str]:
    return (FIX / name).read_text().splitlines()


def test_sequence_matching_by_msgid():
    dut = rb.parse_frames(lines("dut.log"))
    ref = rb.parse_frames(lines("ref.log"))
    assert len(ref) == 10
    assert sorted(v["seq"] for v in dut.values()) == [1, 2, 6, 7, 8, 10]
    assert set(dut) <= set(ref)


def test_non_test_destination_ignored_in_analysis():
    dut = rb.parse_frames(lines("dut.log"))
    assert "DEADBEEF" not in dut  # frame to group 9 with LORATX payload


def test_loss_counting_with_sender_ids():
    s = rb.analyse(lines("dut.log"), lines("ref.log"), sender_lines=lines("sender.log"), arm="B")
    assert s["frames_sent"] == 10
    assert (s["seen_dut"], s["seen_ref"]) == (6, 10)
    assert s["lost_dut_seen_ref"] == 4
    assert s["lost_both"] == 0
    assert s["dut_loss_pct"] == 40.0 and s["ref_loss_pct"] == 0.0


def test_lost_at_both_needs_sent_count():
    ref = lines("ref.log")[:8]  # ref also missed 9 and 10
    dut = [ln for ln in lines("dut.log") if "LORATX 10 " not in ln]
    s = rb.analyse(dut, ref, sent=10)
    assert s["lost_both"] == 2  # 9 and 10 are gone at both
    assert s["lost_dut_seen_ref"] == 3
    assert s["seen_dut"] == 5 and s["seen_ref"] == 8


def test_onrx_p50_max():
    st = rb.onrx_stats(lines("dut.log"))
    assert st["n"] == 3  # malformed ONRX line not counted
    assert st["irq_to_read_us_p50"] == 6000.0
    assert st["irq_to_read_us_max"] == 12000000.0


def test_instr_loop_grouping():
    g = rb.parse_loop_gaps(lines("dut.log"))
    assert g["web"] == {"count": 2, "max_ms": 2400, "total_ms": 3600}
    assert g["lvgl"] == {"count": 1, "max_ms": 310, "total_ms": 310}


def test_empty_arm():
    s = rb.analyse([], [], sent=0)
    assert s["seen_dut"] == s["seen_ref"] == s["lost_both"] == 0
    assert s["dut_loss_pct"] is None
    assert s["onrx_dut"]["irq_to_read_us_p50"] is None and s["onrx_dut"]["irq_to_read_us_max"] is None
    assert s["instr_loop_dut"] == {}
    assert "no gaps" in rb.format_summary(s)
    s2 = rb.analyse(lines("empty.log"), lines("empty.log"), sent=5)
    assert s2["lost_both"] == 5 and s2["dut_loss_pct"] == 100.0


def test_malformed_lines_ignored():
    junk = ["", "[LOG] LORATX", "x LORATX 5 >TEST:", "[INSTR-LOOP];gap;ms;abc;in;web", "[INJ];loratx;q;x;id;zz"]
    s = rb.analyse(junk, junk, sent=3)
    assert s["seen_dut"] == 0 and s["lost_both"] == 3
    assert rb.parse_sender(junk) == {}


def test_sender_call_filter():
    dut = rb.parse_frames(lines("dut.log"), sender_call="DK5EN-14")
    assert dut == {}


def test_main_offline(capsys):
    rc = rb.main(["--analyse", str(FIX / "dut.log"), str(FIX / "ref.log"),
                  "--sender-log", str(FIX / "sender.log"), "--arm", "A"])
    assert rc == 0
    out = capsys.readouterr().out
    assert "lost at DUT but seen at ref 4" in out and "INSTR-LOOP web" in out


def test_guard_rejects_non_test_destination():
    assert rb.require_test_group("TEST") == "TEST"
    for bad in ("9", "99", "", "*", "OE1XAR-62", "test"):
        with pytest.raises(rb.RuleViolation):
            rb.require_test_group(bad)


def test_guard_command_whitelist():
    assert rb.loratx_command(20, 3000) == "--loratx 20 3000"
    for bad in ("--txpower 20", "--setcall X", "--sendmsg TEST hi", "--loratx 21 3000",
                "--loratx 5 50", "--loratx 0 3000", "--loratx 5 3000; --reboot", "--info "):
        with pytest.raises(rb.RuleViolation):
            rb.check_command(bad)
    assert rb.check_command("--info") == "--info"


def test_guard_callsign():
    assert rb.require_fleet_call("DK5EN-92") == "DK5EN-92"
    for bad in ("OE1XAR-62", "XX0XXX-1", "DK5EN"):
        with pytest.raises(rb.RuleViolation):
            rb.require_fleet_call(bad)


def test_resolve_fleet_and_host_override():
    fleet = {"nodes": {"a": {"call": "DK5EN-1", "host": "10.0.0.1"}, "b": {"call": "DK5EN-2", "host": None}}}
    assert rb.resolve("a", None, fleet) == ("a", "10.0.0.1", "DK5EN-1")
    assert rb.resolve("b", "10.0.0.9", fleet)[1] == "10.0.0.9"
    with pytest.raises(rb.RuleViolation):
        rb.resolve("b", None, fleet)
    with pytest.raises(rb.RuleViolation):
        rb.resolve("zz", None, fleet)


def test_sender_counts_only_transmitted_frames():
    # queued 3, only 2 went on air (TX ring lag under a busy channel): the third must not count as lost
    lines = [
        "[INJ];loratx;q;1;id;00C20C7A",
        "[INJ];loratx;q;2;id;00C20C7B",
        "[INJ];loratx;q;3;id;00C20C7C",
        "16:56:55 [LOG] TX x00C20C7A : H04 prio=3 src=o wait=3381 q=2 cad=0 len=37 t=245206",
        "16:57:00 [LOG] TX x00C20C7B : H04 prio=3 src=o wait=5497 q=2 cad=0 len=37 t=250320",
    ]
    assert set(rb.parse_sender(lines)) == {"00C20C7A", "00C20C7B"}


def test_sender_without_tx_lines_keeps_queued_set():
    lines = ["[INJ];loratx;q;1;id;00C20C7A", "[INJ];loratx;q;2;id;00C20C7B"]
    assert set(rb.parse_sender(lines)) == {"00C20C7A", "00C20C7B"}


def test_loop_gap_with_space_separators():
    # printfdeb() prints ';' as ' ' on the console
    lines = ["17:17:29.563 [INSTR-LOOP] gap ms 2047 in udp section_ms 0 sections_ms 0"]
    g = rb.parse_loop_gaps(lines)
    assert g["udp"]["count"] == 1 and g["udp"]["max_ms"] == 2047
