"""Unit tests for mesh_exchange.py -- pure logic and the dry-run driver. No hardware, no serial port."""
import json
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import mesh_exchange as mx  # noqa: E402

MH_HEADER = "[MH] window=720min rows={n}\n"
ROW = ("[MH] call={call} 2026-10-03 11:{mm}:05 typ=pos hw=RAK4631 mod=3/1 rssi=-97dBm snr=7dB "
       "dist=0.0km ncnt=2 age={age}min hm=NA role=- ex=0 nb=0 gw=N lat=N48.123 lon=E011.456 alt=520m\n")


def rows(*specs):
    out = MH_HEADER.format(n=len(specs))
    for call, age in specs:
        out += ROW.format(call=call, mm=10 + age, age=age)
    return out


# ---------------------------------------------------------------- parse_mheard

def test_parse_current_format_three_peers():
    text = "--mheard\n" + rows(("DK5EN-1", 1), ("DK5EN-14", 2), ("DK5EN-92", 0))
    assert mx.parse_mheard(text) == {"DK5EN-1", "DK5EN-14", "DK5EN-92"}


def test_parse_empty_window():
    assert mx.parse_mheard("--mheard\n[MH] window=720min rows=0\n") == set()
    assert mx.parse_mheard("") == set()


def test_parse_age_filter_drops_stale_rows():
    text = rows(("DK5EN-1", 3), ("DK5EN-92", 400))
    assert mx.parse_mheard(text) == {"DK5EN-1", "DK5EN-92"}
    assert mx.parse_mheard(text, max_age_min=5) == {"DK5EN-1"}


def test_parse_crlf_and_foreign_stations_kept():
    text = rows(("DL1ABC-11", 1), ("DK5EN-90", 0)).replace("\n", "\r\n")
    assert mx.parse_mheard(text) == {"DL1ABC-11", "DK5EN-90"}


def test_parse_ignores_live_log_lines_when_rows_exist():
    text = (
        "MH-LoRa;123;DK5EN-98;DK5EN-1;2\n"           # live loradebug line of another station
        + rows(("DK5EN-14", 1))
        + "RX-LoRa2;DK5EN-92;-97\n"
    )
    assert mx.parse_mheard(text) == {"DK5EN-14"}


def test_parse_fallback_free_form_skips_live_lines_and_echo():
    text = (
        "--mheard\n"
        "MHeard list\n"
        "DK5EN-92  -101 dBm  7 dB  12:01:02\n"
        "DK5EN-14  -95 dBm  9 dB  12:01:30\n"
        "MH-LoRa;7;DK5EN-98;DK5EN-1;2\n"
    )
    assert mx.parse_mheard(text) == {"DK5EN-92", "DK5EN-14"}


# --------------------------------------------------------------- matrix/verdict

def full_heard(calls):
    return {b: {a for a in calls if a != b} for b in calls}


@pytest.mark.parametrize("calls", [
    ["DK5EN-1", "DK5EN-90"],
    ["DK5EN-1", "DK5EN-90", "DK5EN-92"],
    ["DK5EN-1", "DK5EN-14", "DK5EN-90", "DK5EN-92"],
])
def test_full_exchange_passes(calls):
    m = mx.build_matrix(calls, full_heard(calls))
    assert mx.verdict(m) == (True, [])
    for a in calls:
        assert set(m[a]) == set(calls) - {a}      # no self pair


def test_two_nodes_one_direction_missing():
    calls = ["DK5EN-1", "DK5EN-90"]
    heard = {"DK5EN-1": {"DK5EN-90"}, "DK5EN-90": set()}
    m = mx.build_matrix(calls, heard)
    assert m["DK5EN-1"]["DK5EN-90"] is False       # 90 did not hear 1
    assert m["DK5EN-90"]["DK5EN-1"] is True
    assert mx.verdict(m) == (False, [("DK5EN-1", "DK5EN-90")])


def test_three_nodes_one_missing_pair():
    calls = ["DK5EN-1", "DK5EN-90", "DK5EN-92"]
    heard = full_heard(calls)
    heard["DK5EN-92"].discard("DK5EN-90")
    ok, missing = mx.verdict(mx.build_matrix(calls, heard))
    assert not ok and missing == [("DK5EN-90", "DK5EN-92")]


def test_four_nodes_one_missing_pair_and_self_entry_ignored():
    calls = ["DK5EN-1", "DK5EN-14", "DK5EN-90", "DK5EN-92"]
    heard = full_heard(calls)
    heard["DK5EN-14"].add("DK5EN-14")              # a node listing itself changes nothing
    heard["DK5EN-1"].discard("DK5EN-92")
    m = mx.build_matrix(calls, heard)
    ok, missing = mx.verdict(m)
    assert not ok and missing == [("DK5EN-92", "DK5EN-1")]
    assert "DK5EN-14" not in m["DK5EN-14"]


def test_missing_listener_entry_counts_as_not_heard():
    m = mx.build_matrix(["A-1", "B-2"], {"A-1": {"B-2"}})
    assert mx.verdict(m) == (False, [("A-1", "B-2")])


# ------------------------------------------------------------------- summarize

def test_summarize_pass_wording():
    calls = ["DK5EN-1", "DK5EN-90", "DK5EN-92"]
    s = mx.summarize(calls, mx.build_matrix(calls, full_heard(calls)))
    assert "PASS: all 6 pairs heard (3 nodes)" in s
    assert "FAIL" not in s and "NO" not in s


def test_summarize_fail_names_the_pair():
    calls = ["DK5EN-1", "DK5EN-90"]
    m = mx.build_matrix(calls, {"DK5EN-1": {"DK5EN-90"}, "DK5EN-90": set()})
    s = mx.summarize(calls, m)
    assert "FAIL: 1 of 2 pairs missing" in s
    assert "DK5EN-1 not heard by DK5EN-90" in s
    assert "NO" in s


# ---------------------------------------------------------------------- driver

def test_dry_run_prints_plan_and_opens_nothing(capsys):
    calls = []

    def factory(*a, **k):
        calls.append(a)
        raise AssertionError("session factory must not be called in --dry-run")

    rc = mx.main(["--dry-run", "--port", "/dev/null", "--port", "/dev/null", "--settle", "10", "--spacing", "5"],
                 session_factory=factory)
    out = capsys.readouterr().out
    assert rc == 0
    assert calls == []
    assert "mesh_exchange plan: 2 nodes" in out
    assert "--sendpos from each node in turn, 5s apart" in out
    assert "--loradebug off" in out


def test_needs_two_ports():
    with pytest.raises(SystemExit) as e:
        mx.main(["--dry-run", "--port", "/dev/null"])
    assert e.value.code == 2


class FakeSession:
    """Scripted node: answers --info and --mheard from canned text, records every command."""

    def __init__(self, port, info, mheard, log):
        self.port, self._info, self._mheard, self.log = port, info, mheard, log
        self.lines = []
        self.closed = False

    def send(self, cmd):
        idx = len(self.lines)
        self.log.append((self.port, cmd))
        if cmd == "--info":
            self.lines += self._info.split("\n")
        elif cmd == "--mheard":
            self.lines += self._mheard.split("\n")
        return idx

    def pump(self, seconds):
        return True

    def wait_for(self, pattern, timeout, since=0):
        return None

    def lines_since(self, idx):
        return self.lines[idx:]

    def close(self):
        self.closed = True


def info_for(call, power=2):
    return (f"...UPDATE: 2026-10-03 11:00:00\n...Call: <{call}> ...ID 1 ...NODE 9 <RAK4631> ...UTC-OFF 2.0 [{{CET}}]\n"
            f"...FREQ 433.1750 MHz TXPWR {power} dBm RXBOOST off\n...Webserver  on / Gateway off\n...hasIpAddress: yes\n...IP address : 192.168.68.9\n")


def run(monkeypatch, tmp_path, infos, mheards, argv_extra=()):
    log, made = [], []

    def factory(port, dtr, log_dir):
        s = FakeSession(port, infos[port], mheards[port], log)
        made.append(s)
        return s

    monkeypatch.setattr(mx.time, "sleep", lambda s: None)
    monkeypatch.setattr(mx, "_pump_all", lambda sessions, seconds: None)
    out = tmp_path / "x.json"
    ports = list(infos)
    argv = [x for p in ports for x in ("--port", p)] + ["--out", str(out), "--settle", "0", "--spacing", "1", *argv_extra]
    rc = mx.main(argv, session_factory=factory)
    return rc, log, made, out


def test_driver_pass_with_fake_sessions(monkeypatch, tmp_path):
    infos = {"/p/a": info_for("DK5EN-90"), "/p/b": info_for("DK5EN-92")}
    heard = {"/p/a": rows(("DK5EN-92", 0)), "/p/b": rows(("DK5EN-90", 1))}
    rc, log, made, out = run(monkeypatch, tmp_path, infos, heard)
    assert rc == 0
    res = json.loads(out.read_text())
    assert res["ok"] is True and res["missing"] == []
    assert res["calls"] == ["DK5EN-90", "DK5EN-92"]
    assert set(res["mheard_raw"]) == {"DK5EN-90", "DK5EN-92"}
    cmds = [c for _, c in log]
    assert cmds.count("--sendpos") == 2
    assert cmds.count("--loradebug on") == 2 and cmds.count("--loradebug off") == 2
    assert all(s.closed for s in made)


def test_driver_missing_pair_exits_1(monkeypatch, tmp_path):
    infos = {"/p/a": info_for("DK5EN-90"), "/p/b": info_for("DK5EN-92")}
    heard = {"/p/a": rows(("DK5EN-92", 0)), "/p/b": "--mheard\n[MH] window=720min rows=0\n"}
    rc, _, _, out = run(monkeypatch, tmp_path, infos, heard)
    assert rc == 1
    assert json.loads(out.read_text())["missing"] == [["DK5EN-90", "DK5EN-92"]]


def test_driver_guard_refusal_transmits_nothing(monkeypatch, tmp_path):
    infos = {"/p/a": info_for("DK5EN-90"), "/p/b": info_for("XX0XXX-00")}
    rc, log, made, out = run(monkeypatch, tmp_path, infos, {"/p/a": "", "/p/b": ""})
    cmds = [c for _, c in log]
    assert rc == 1
    assert "--sendpos" not in cmds and "--loradebug on" not in cmds
    assert all(s.closed for s in made)
    assert json.loads(out.read_text())["ok"] is False


def test_driver_guard_refuses_power_above_bench_limit(monkeypatch, tmp_path):
    infos = {"/p/a": info_for("DK5EN-90"), "/p/b": info_for("DK5EN-92", power=20)}
    rc, log, _, _ = run(monkeypatch, tmp_path, infos, {"/p/a": "", "/p/b": ""})
    assert rc == 1 and "--sendpos" not in [c for _, c in log]


def test_loradebug_restored_when_sequence_raises(monkeypatch, tmp_path):
    infos = {"/p/a": info_for("DK5EN-90"), "/p/b": info_for("DK5EN-92")}
    log, made = [], []

    class Boom(FakeSession):
        def send(self, cmd):
            if cmd == "--mheard":
                raise RuntimeError("port dropped")
            return super().send(cmd)

    def factory(port, dtr, log_dir):
        s = Boom(port, infos[port], "", log)
        made.append(s)
        return s

    monkeypatch.setattr(mx, "_pump_all", lambda sessions, seconds: None)
    with pytest.raises(RuntimeError):
        mx.main(["--port", "/p/a", "--port", "/p/b", "--settle", "0", "--spacing", "1"], session_factory=factory)
    cmds = [c for _, c in log]
    assert cmds.count("--loradebug off") == 2
    assert all(s.closed for s in made)
