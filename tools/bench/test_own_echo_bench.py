"""Unit tests for the own-echo bench driver (no hardware). Log shapes copied from DK5EN-98 logs."""

from __future__ import annotations

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import own_echo_bench as o  # noqa: E402
import gwrx_bench as gb  # noqa: E402

NODES = ["DK5EN-98", "DK5EN-1", "DK5EN-90"]


# ------------------------------------------------------------------ guard


@pytest.mark.parametrize("dest", ["9999", "TEST", "@DK5EN-1", "@DK5EN-14", "@DK5EN-90", "@DK5EN-92", "@DK5EN-98"])
def test_guard_accepts(dest):
    c = o.parse_case(f"E1:DK5EN-98:{dest}" if dest != "@DK5EN-98" else "E1:DK5EN-1:@DK5EN-98")
    assert c.pn == dest.startswith("@")
    assert c.dest == dest.lstrip("@")


@pytest.mark.parametrize("dest", ["*", "", "20", "232", "262", "9", "26244", "test", "@OE1XAR-1", "OE1XAR-1",
                                  "DK5EN-1", "@", "@*", "@XX0XXX-1", "@DK5EN-97"])
def test_guard_refuses_destination(dest):
    with pytest.raises(gb.PlanError):
        o.parse_case(f"X:DK5EN-98:{dest}")


def test_guard_refuses_sender_and_self_pn():
    with pytest.raises(gb.PlanError):
        o.parse_case("X:OE1XAR-1:9999")
    with pytest.raises(gb.PlanError):
        o.parse_case("X:DK5EN-97:9999")
    with pytest.raises(gb.PlanError):
        o.parse_case("X:DK5EN-98:@DK5EN-98")
    with pytest.raises(gb.PlanError):
        o.parse_case("X:DK5EN-98")


def test_guard_sender_must_be_node_and_not_airgapped():
    c = o.parse_case("E1:DK5EN-98:9999")
    o.check_cases([c], NODES, [])
    with pytest.raises(gb.PlanError, match="not a --node"):
        o.check_cases([o.parse_case("E8:DK5EN-14:9999")], NODES, [])
    with pytest.raises(gb.PlanError, match="airgap"):
        o.check_cases([c], NODES, ["DK5EN-98"])
    with pytest.raises(gb.PlanError, match="not a --node"):
        o.check_cases([c], NODES, ["DK5EN-92"])
    with pytest.raises(gb.PlanError):
        o.check_cases([], NODES, [])


def test_node_call_guard():
    o.check_node_call("DK5EN-92")
    with pytest.raises(gb.PlanError):
        o.check_node_call("DK5EN-97")


@pytest.mark.parametrize("cmd", ["--gateway on", "--gateway off", "--setgrc 20", "--mesh on", "--txpower 22",
                                 "--reboot", "::{20}EC E1 1/10 101010", "::{*}EC E1 1/10 101010",
                                 "::{OE1XAR-1}EC E1 1/10 101010", "::{9999}hello", "--airgap"])
def test_console_whitelist_refuses(cmd):
    with pytest.raises(gb.PlanError):
        o.check_console_cmd(cmd)


@pytest.mark.parametrize("cmd", ["--info", "--loradebug on", "--txcapture off", "--airgap on",
                                 "::{9999}EC E1 1/10 101010", "::{TEST}EC E3 10/10 101010",
                                 "::{DK5EN-1}EC E4 2/10 101010"])
def test_console_whitelist_accepts(cmd):
    o.check_console_cmd(cmd)


def test_cli_refusal_exits_before_opening(monkeypatch):
    def boom(*a, **k):
        raise AssertionError("opened a transport")

    monkeypatch.setattr(gb, "make_transport", boom)
    with pytest.raises(SystemExit) as e:
        o.main(["--node", "DK5EN-98=tcp:1.2.3.4", "--case", "X:DK5EN-98:20"])
    assert "refused" in str(e.value)
    with pytest.raises(SystemExit):
        o.main(["--node", "DK5EN-98=tcp:1.2.3.4", "--case", "E1:DK5EN-1:9999"])
    assert o.main(["--dry-run", "--node", "DK5EN-98=tcp:1.2.3.4", "--case", "E1:DK5EN-98:9999"]) == 0


# ------------------------------------------------------- spec / schedule / text


def test_case_spec_parsing():
    assert o.parse_case("E4:dk5en-98:@DK5EN-1") == o.Case("E4", "DK5EN-98", "DK5EN-1", True)
    assert o.parse_case("E3:DK5EN-98:TEST") == o.Case("E3", "DK5EN-98", "TEST", False)


def test_schedule_offsets_round_robin():
    cases = [o.parse_case("E1:DK5EN-98:9999"), o.parse_case("E6:DK5EN-1:9999"), o.parse_case("E8:DK5EN-14:9999")]
    s = o.schedule(cases, 2, 90)
    assert [(t, k, i) for t, k, i in s] == [(0, 0, 1), (30, 1, 1), (60, 2, 1), (90, 0, 2), (120, 1, 2), (150, 2, 2)]


def test_message_builder():
    c = o.parse_case("E4:DK5EN-98:@DK5EN-1")
    t = o.build_text("E4", 3, 10, "123456")
    assert t == "EC E4 3/10 123456" and t.isascii()
    assert o.console_line(c, t) == "::{DK5EN-1}EC E4 3/10 123456"
    assert o.console_line(o.parse_case("E3:DK5EN-98:TEST"), t) == "::{TEST}EC E4 3/10 123456"
    o.check_console_cmd(o.console_line(c, t))


# ---------------------------------------------------------------- analysis
# Line shapes copied from the DK5EN-98 rpizero logs (2026-10-24/25).


def L(sec: int, body: str) -> str:
    return f"2026-10-25 08:03:{sec:02d}.000  {body}"


def ring_write(sec, mid, src="user_msg"):
    return L(sec, f"[MC-DBG] RING_WRITE slot=0 type=3A status=00 len=39 msg_id={mid} queued=0/20 src={src}")


def new_txt(sec, mid, dst, text, src="DK5EN-98"):
    return L(sec, f"08:03:{sec:02d} NEW-TXT 039 : x{mid} H04 S0 T0 M00 {src}>{dst}:{text} HW:43 MOD:8/8 FCS:0A54 FW:40:a LH:AB")


def tx_udp(sec, mid, dst, text, src="DK5EN-98"):
    return L(sec, f"08:03:{sec:02d} TX-UDP   039 : x{mid} H04 S0 T0 M01 {src}>{dst}:{text} HW:43 MOD:8/8 FCS:0A54 FW:40:a LH:AB")


def tx_lora(sec, mid, dst, text, src="DK5EN-98"):
    return [
        L(sec, f"[MC-DBG] RING_TX_READ slot=0 prio=4 type=3A status=FF len=39 msg_id={mid} retry=0 queued=1/20 lat=3629ms"),
        L(sec, f"08:03:{sec:02d} TX-LoRa  039 : x{mid} H04 S0 T0 M01 {src}>{dst}:{text} HW:43 MOD:8/8 FCS:0A54 FW:40:a LH:AB"),
    ]


def released(sec, mid):
    return [
        L(sec, "[RETX] echo on READY slot, released retid:0"),
        L(sec, f"08:03:{sec:02d} checkOwnTx:{mid} own_msg_id:{mid} <{mid}> 00"),
        L(sec, f"[MC-DBG] RX_DEDUP_DUP msg_id={mid} slot=29"),
    ]


def heard(sec, mid, path, dst, text):
    return L(sec, f"08:03:{sec:02d} [LOG] 142 : x{mid} H04 S1 T0 M00 {path}>{dst}:{text} HW:43 MOD:8/8 FCS:2EE2 FW:40:a LH:89 RSSI:-82 SNR:6 DUP:d OWN:e")


def send(case, i, sender, dest, marker, pn=False):
    return {"case": case, "i": i, "n": 10, "marker": marker, "sender": sender, "dest": dest, "pn": pn}


def test_released_before_tx_and_released_by():
    mk = "EC E1 1/10 080341"
    sender = [ring_write(41, "1AE1E14E"), new_txt(41, "1AE1E14E", "9999", mk), tx_udp(41, "1AE1E14E", "9999", mk)]
    sender += released(44, "1AE1E14E") + [heard(44, "1AE1E14E", "DK5EN-98,DK5EN-90", "9999", mk)]
    sender += [L(50, "[MC-DBG] RING_STATUS queued=0 pending=0 retrying=0 done=1 dedup=100/100")]
    res = o.analyse([send("E1", 1, "DK5EN-98", "9999", mk)], {"DK5EN-98": sender})
    m = res["messages"][0]
    assert m["id"] == "1AE1E14E"
    assert m["verdict"] == "RELEASED"          # TX-UDP alone is not a LoRa TX
    assert m["released_by"] == "DK5EN-90"      # last call of the echoing path
    assert m["release_after_write_s"] == 3.0
    assert res["cases"]["E1"]["released"] == 1 and res["cases"]["E1"]["tx"] == 0
    assert res["cases"]["E1"]["released_by"] == {"DK5EN-90": 1}


def test_sent_then_echo_after_and_before():
    mk = "EC E1 2/10 080341"
    sender = [ring_write(41, "1AE1E150"), new_txt(41, "1AE1E150", "9999", mk)]
    sender += tx_lora(43, "1AE1E150", "9999", mk) + [heard(47, "1AE1E150", "DK5EN-98,DL2JA-1", "9999", mk)]
    res = o.analyse([send("E1", 2, "DK5EN-98", "9999", mk)], {"DK5EN-98": sender})
    m = res["messages"][0]
    assert (m["verdict"], m["own_tx"], m["echo_vs_own_tx"]) == ("SENT", 1, "after")
    assert m["first_tx_after_write_s"] == 2.0
    # echo heard (DUP) while TX line comes later: "before"
    mk2 = "EC E1 3/10 080341"
    s2 = [ring_write(41, "1AE1E151"), new_txt(41, "1AE1E151", "9999", mk2),
          heard(42, "1AE1E151", "DK5EN-98,DK5EN-1", "9999", mk2)] + tx_lora(43, "1AE1E151", "9999", mk2)
    m2 = o.analyse([send("E1", 3, "DK5EN-98", "9999", mk2)], {"DK5EN-98": s2})["messages"][0]
    assert (m2["verdict"], m2["echo_vs_own_tx"]) == ("SENT", "before")


def test_unknown_without_tx_or_release_and_without_marker():
    mk = "EC E1 4/10 080341"
    s = [ring_write(41, "1AE1E152"), new_txt(41, "1AE1E152", "9999", mk)]
    m = o.analyse([send("E1", 4, "DK5EN-98", "9999", mk)], {"DK5EN-98": s})["messages"][0]
    assert m["verdict"] == "UNKNOWN" and m["id"] == "1AE1E152"
    m = o.analyse([send("E1", 5, "DK5EN-98", "9999", "EC E1 5/10 080341")], {"DK5EN-98": s})["messages"][0]
    assert m["verdict"] == "UNKNOWN" and m["id"] is None


def test_marker_prefix_does_not_collide():
    # "1/10 080341" vs "1/100 ..." style: marker followed by a digit must not match
    s = [new_txt(41, "1AE1E160", "9999", "EC E1 1/10 0803419")]
    assert o.NodeEvents(s).find_id("EC E1 1/10 080341") is None


def test_pn_ack_skip_then_tx_then_server_ack():
    mk = "EC E4 1/10 080341"
    mid = "1AE1E09D"
    s = [ring_write(41, mid), new_txt(41, mid, "DK5EN-1", mk + "{123"), tx_udp(41, mid, "DK5EN-1", mk + "{123"),
         L(42, f"[MC-DBG] ACK_SKIP_READY slot=0 msg_id={mid}")]
    s += tx_lora(43, mid, "DK5EN-1", mk + "{123")
    s += [L(83, f"[MC-DBG] RETRANSMIT retry=1 after_sec=40 msg_id={mid}")]
    s += tx_lora(84, mid, "DK5EN-1", mk + "{123")
    s += [L(86, f"[RETX] server ACK for retid:0 stop retransmit msg-id:{mid}")]
    res = o.analyse([send("E4", 1, "DK5EN-98", "DK5EN-1", mk, pn=True)], {"DK5EN-98": s})
    m = res["messages"][0]
    assert m["pn"] == {"ack_skip_ready": True, "own_tx": 2, "retransmit_retries": [1], "acks": ["server"]}
    assert m["verdict"] == "SENT"
    c = res["cases"]["E4"]
    assert (c["pn_ack_skip"], c["pn_tx_total"], c["pn_acks"]) == (1, 2, {"server": 1})


def test_pn_ack_kinds():
    ev = o.NodeEvents([
        L(1, "[RETX] binary ACK for retid:0 stop retransmit msg-id:1AE1E155"),
        L(2, "[RETX] PN already acked, stop retid:3 msg-id:1AE1E0FD"),
    ])
    assert [k for *_, k in ev.acks["1AE1E155"]] == ["binary"]
    assert [k for *_, k in ev.acks["1AE1E0FD"]] == ["already"]


def test_field_echo_via_dl2ja1_on_each_node():
    mk = "EC E1 6/10 080341"
    mid = "1AE1E170"
    s98 = [ring_write(41, mid), new_txt(41, mid, "9999", mk)] + tx_lora(43, mid, "9999", mk)
    s98.append(heard(47, mid, "DK5EN-98,DL2JA-1", "9999", mk))
    s1 = [heard(48, mid, "DK5EN-98,DB0ED-99,DF2SI-12", "9999", mk), heard(49, mid, "DK5EN-98,DK5EN-90", "9999", mk)]
    res = o.analyse([send("E1", 6, "DK5EN-98", "9999", mk)], {"DK5EN-98": s98, "DK5EN-1": s1})
    m = res["messages"][0]
    assert m["nodes"]["DK5EN-98"]["field"] == ["DL2JA-1"]
    assert m["nodes"]["DK5EN-1"]["field"] == ["DB0ED-99", "DF2SI-12"]
    assert m["field_echo"] == ["DB0ED-99", "DF2SI-12", "DL2JA-1"]
    assert res["cases"]["E1"]["field_echo"]["DL2JA-1"] == 1
    # configurable list
    res2 = o.analyse([send("E1", 6, "DK5EN-98", "9999", mk)], {"DK5EN-98": s98}, field=("DB0ED-99",))
    assert res2["messages"][0]["field_echo"] == []


def test_udp_rx_queue_and_tx_on_another_node():
    mk = "EC E6 1/10 080341"
    mid = "1AE1E180"
    s1 = [ring_write(41, mid), new_txt(41, mid, "9999", mk, src="DK5EN-1"), released(44, mid)[0],
          released(44, mid)[1], heard(44, mid, "DK5EN-1,DK5EN-98", "9999", mk)]
    s98 = [ring_write(42, mid, src="udp_rx")] + tx_lora(43, mid, "9999", mk, src="DK5EN-1")
    s90 = [heard(44, mid, "DK5EN-1,DK5EN-98", "9999", mk)]
    res = o.analyse([send("E6", 1, "DK5EN-1", "9999", mk)], {"DK5EN-1": s1, "DK5EN-98": s98, "DK5EN-90": s90})
    m = res["messages"][0]
    assert m["verdict"] == "RELEASED" and m["released_by"] == "DK5EN-98"
    assert m["nodes"]["DK5EN-98"]["queued_from_server"] is True
    assert m["nodes"]["DK5EN-98"]["tx"] == 1
    assert m["nodes"]["DK5EN-1"]["queued_from_server"] is False and m["nodes"]["DK5EN-1"]["tx"] == 0
    assert len(m["nodes"]["DK5EN-90"]["heard"]) == 1


def test_release_id_taken_from_next_checkownTx_only():
    # a release line without a following checkOwnTx in range must not release anything
    lines = [L(1, "[RETX] echo on READY slot, released retid:0")] + [L(2, "x")] * 9 + [
        L(3, "08:03:03 checkOwnTx:1AE1E190 own_msg_id:1AE1E190 <1AE1E190> 00")]
    assert o.NodeEvents(lines).released == {}


def test_node_log_prefix_format():
    # gb.Node log: "HH:MM:SS " in front of the (possibly itself stamped) console line
    mk = "EC E1 7/10 080341"
    lines = [f"08:03:41 08:03:41 NEW-TXT 039 : x1AE1E1A0 H04 S0 T0 M00 DK5EN-98>9999:{mk} HW:43",
             "08:03:43 [MC-DBG] RING_TX_READ slot=0 prio=4 type=3A status=FF len=39 msg_id=1AE1E1A0 retry=0 queued=1/20 lat=1ms"]
    m = o.analyse([send("E1", 7, "DK5EN-98", "9999", mk)], {"DK5EN-98": lines})["messages"][0]
    assert (m["id"], m["verdict"], m["first_tx_after_write_s"]) == ("1AE1E1A0", "SENT", 2.0)


def test_extract_sends_and_reanalyse_dir(tmp_path):
    mk = "EC E1 1/2 080341"
    log = [f"08:03:41 >>> ::{{9999}}{mk}", "08:03:41 >>> ::{DK5EN-1}EC E4 1/2 080341",
           "08:03:42 >>> --info"]
    sends = o.extract_sends("DK5EN-98", log)
    assert [(s["case"], s["dest"], s["pn"]) for s in sends] == [("E1", "9999", False), ("E4", "DK5EN-1", True)]
    (tmp_path / "own-echo-A-dk5en-98-20261025-080000.log").write_text(
        "\n".join(log + [f"08:03:41 08:03:41 NEW-TXT 039 : x1AE1E1B0 H04 S0 T0 M00 DK5EN-98>9999:{mk} HW:43",
                         "08:03:43 [MC-DBG] RING_TX_READ slot=0 prio=4 type=3A status=FF len=39 "
                         "msg_id=1AE1E1B0 retry=0 queued=1/20 lat=1ms"]))
    groups = o.find_groups(tmp_path, "20261025-080000")
    assert list(groups) == [("A", "20261025-080000")]
    assert list(groups[("A", "20261025-080000")]) == ["DK5EN-98"]
    assert o.main(["--reanalyse-dir", str(tmp_path)]) == 0
    assert (tmp_path / "own-echo-A-20261025-080000-reanalysis.json").exists()
    r = o.reanalyse_group(groups[("A", "20261025-080000")], o.FIELD_CALLS)
    assert r["cases"]["E1"]["tx"] == 1 and r["cases"]["E4"]["unknown"] == 1
