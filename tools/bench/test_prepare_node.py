"""Unit tests for prepare_node.py -- pure logic and the driver against a fake session. No hardware, no serial port."""
import json
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import prepare_node as pn  # noqa: E402

FLEET = {
    "max_txpower_dbm": 2,
    "taken_ssids": {"14": "x", "90": "x", "92": "x", "1": "x"},
    "nodes": {
        "t-deck-14": {"call": "DK5EN-14", "board": "t_deck_plus", "family": "esp32"},
        "heltec-1": {"call": "DK5EN-1", "board": "heltec_wifi_lora_32_V3", "family": "esp32"},
        "t-beam-92": {"call": "DK5EN-92", "board": "ttgo_tbeam", "family": "esp32"},
        "rak-90": {"call": "DK5EN-90", "board": "wiscore_rak4631", "family": "nrf52", "host": None},
    },
}


def info_text(call="DK5EN-14", groups=(20, 232, 262, 26244), debug=True, sep="csv", lang="en",
              power=2, nrf=False):
    """A --info reply built from the firmware format strings (src/command_functions.cpp ~6180-6255)."""
    gc = "".join(f"GC-{i + 1}:{g} " for i, g in enumerate(groups) if g > 0)
    gc += "".join(f"GC-{i + 1}:0 " for i in range(len(groups), 6)) if groups else ""
    web = "" if nrf else "...Webserver  on / Gateway off\n...hasIpAddress: yes\n...IP address : 192.168.68.9\n"
    return (
        "--MeshCom 4.40a (build: Oct  3 2026 / 20:15:12)\n"
        "...UPDATE: 2026-10-04 08:00:00\n"
        f"...Call: <{call}> ...ID 00ABCDEF ...NODE 43 <T-Deck> ...UTC-OFF 2.000000 [NTP]\n"
        "...BATT 4.10 V ...BATT 93 % ...MAXV 4.200 V\n...TIME 123456 ms\n"
        "...Flash-Version 17\n"
        "...NOMSGALL off ...MESH on ...BUTTON (0) off ...SOFTSER off ... SOFTSERREAD off\n...PASSWD <***>\n"
        f"...DEBUG {sep} ...DEBUG {lang}\n"
        f"...DEBUG {'on' if debug else 'off'} ...LORADEBUG off ...NBRDEBUG off ...TXCAPTURE off ...GPSDEBUG off/0 ...SOFTSERDEBUG off\n"
        "...WXDEBUG off ...BLEDEBUG off\n"
        f"...FREQ 433.1750 MHz TXPWR {power} dBm RXBOOST off\n"
        "...MAXHOP text 5 / pos 4\n"
        + web
        + ("\n..." + gc + "\n" if gc else "")
    )


def pos_text(gps=True, track=False):
    """The --pos reply tail (src/command_functions.cpp ~6417); --info does not print GPS/Track."""
    return ("\n\nMeshCom 4.40a\n...LAT: 48.1234 N\n...LON: 11.4567 E\n...ALT: 520\n"
            f"...SYMB: # & ..Auto off\n...GPS: {'on' if gps else 'off'}\n...Track: {'on' if track else 'off'}\n"
            "...SOFTSER: off APP:0\n...SOFTSERREAD: off\n")


GOOD_GROUPS = (9, 20, 232, 262)


# ---------------------------------------------------------------- parse_state

def test_parse_state_realistic_info_with_trailing_zero_slots():
    text = info_text(groups=(20, 232, 262, 26244), debug=True, sep="csv", lang="en")
    assert "GC-1:20 GC-2:232 GC-3:262 GC-4:26244 GC-5:0 GC-6:0" in text
    st = pn.parse_state(text)
    assert st["groups"] == [20, 232, 262, 26244]
    assert st["debug"] is True and st["separator"] == "csv" and st["language"] == "en"
    assert st["gps"] is None and st["track"] is None       # --info does not print them


def test_parse_state_debug_off_man_de_and_crlf():
    text = info_text(debug=False, sep="man", lang="de").replace("\n", "\r\n")
    st = pn.parse_state(text)
    assert st["debug"] is False and st["separator"] == "man" and st["language"] == "de"


def test_parse_state_gps_and_track_from_the_pos_reply():
    st = pn.parse_state(info_text() + pos_text(gps=True, track=False))
    assert st["gps"] is True and st["track"] is False
    st = pn.parse_state(info_text() + pos_text(gps=False, track=True))
    assert st["gps"] is False and st["track"] is True


def test_parse_state_no_group_tokens_means_empty_list_and_last_list_wins():
    assert pn.parse_state(info_text(groups=()))["groups"] == []
    text = info_text(groups=(1, 2)) + info_text(groups=GOOD_GROUPS)
    assert pn.parse_state(text)["groups"] == [9, 20, 232, 262]


def test_parse_state_garbage_is_all_unknown():
    st = pn.parse_state("nothing useful\n")
    assert st == {"groups": [], "debug": None, "separator": None, "language": None, "gps": None, "track": None,
                  "lat": None, "lon": None}


# ---------------------------------------------------------------- plan_changes

def state(groups=GOOD_GROUPS, debug=None, separator=None, gps=None, track=None,
          lat=48.408, lon=11.738):
    return {"groups": list(groups), "debug": debug, "separator": separator, "language": "en",
            "gps": gps, "track": track, "lat": lat, "lon": lon}


def test_plan_wrong_groups_is_one_setgrc_on_every_board():
    for board in ("t_deck_plus", "heltec_wifi_lora_32_V3", "ttgo_tbeam", "wiscore_rak4631"):
        good = {"t_deck_plus": dict(debug=True, separator="csv"),
                "heltec_wifi_lora_32_V3": dict(debug=False, gps=True),
                "ttgo_tbeam": dict(gps=True, track=False),
                "wiscore_rak4631": {}}[board]
        assert pn.plan_changes(state(groups=(20, 232, 262, 26244), **good), board) == ["--setgrc 9;20;232;262"]
        assert pn.plan_changes(state(groups=(), **good), board) == ["--setgrc 9;20;232;262"]
        assert pn.plan_changes(state(**good), board) == []


def test_plan_tdeck_debug_off_needs_debug_on_and_csv():
    for board in ("t_deck_plus", "t_deck"):
        assert pn.plan_changes(state(debug=False, separator="man"), board) == ["--debug on", "--debug csv"]
        assert pn.plan_changes(state(debug=True, separator="man"), board) == ["--debug csv"]
        assert pn.plan_changes(state(debug=False, separator="csv"), board) == ["--debug on"]
        assert pn.plan_changes(state(debug=True, separator="csv"), board) == []


def test_plan_heltec_debug_on_and_gps_off():
    b = "heltec_wifi_lora_32_V3"
    assert pn.plan_changes(state(debug=True, gps=True), b) == ["--debug off"]
    assert pn.plan_changes(state(debug=False, gps=False), b) == ["--gps on"]
    assert pn.plan_changes(state(debug=True, gps=False), b) == ["--debug off", "--gps on"]
    assert pn.plan_changes(state(debug=False, gps=True), b) == []


def test_plan_tbeam_track_on_and_gps_off():
    b = "ttgo_tbeam"
    assert pn.plan_changes(state(gps=True, track=True), b) == ["--track off"]
    assert pn.plan_changes(state(gps=False, track=False), b) == ["--gps on"]
    assert pn.plan_changes(state(gps=False, track=True), b) == ["--gps on", "--track off"]
    assert pn.plan_changes(state(debug=True, gps=True, track=False), b) == []     # debug is no T-Beam requirement


def test_plan_rak_has_no_debug_requirement_and_groups_come_first():
    assert pn.plan_changes(state(debug=True, separator="man", gps=False, track=True), "wiscore_rak4631") == []
    assert pn.plan_changes(state(groups=(1,), debug=True, separator="man"), "t_deck_plus") == \
        ["--setgrc 9;20;232;262", "--debug csv"]


def test_plan_unseen_value_counts_as_missing_and_group_order_is_irrelevant():
    assert pn.plan_changes(state(gps=None, track=None), "ttgo_tbeam") == ["--gps on", "--track off"]
    assert pn.plan_changes(state(groups=(262, 232, 20, 9)), "wiscore_rak4631") == []
    assert pn.plan_changes(state(groups=(9, 20, 232)), "wiscore_rak4631") == ["--setgrc 9;20;232;262"]
    assert pn.plan_changes(state(), "unknown_board") == []


def test_needs_pos_for_gps_track_or_position_boards():
    assert pn.needs_pos("ttgo_tbeam") and pn.needs_pos("heltec_wifi_lora_32_V3")
    assert pn.needs_pos("wiscore_rak4631")            # bench position, no GPS
    assert not pn.needs_pos("t_deck_plus")


def test_plan_unpositioned_rak_gets_the_bench_qth_once():
    want = ["--setlat 48.408", "--setlon 11.738", "--setalt 484"]
    assert pn.plan_changes(state(lat=0.0, lon=0.0), "wiscore_rak4631") == want
    assert pn.plan_changes(state(lat=None, lon=None), "wiscore_rak4631") == want
    assert pn.plan_changes(state(lat=48.4076, lon=11.7384), "wiscore_rak4631") == []
    assert pn.plan_changes(state(lat=0.0, lon=0.0), "ttgo_tbeam") == ["--gps on", "--track off"]


def test_parse_state_reads_lat_lon_with_hemisphere():
    st = pn.parse_state("...LAT: 48.4076 N\n...LON: 11.7384 E\n")
    assert st["lat"] == 48.4076 and st["lon"] == 11.7384
    st = pn.parse_state("...LAT: 33.8688 S\n...LON: 151.2093 E\n")
    assert st["lat"] == -33.8688 and st["lon"] == 151.2093


# ---------------------------------------------------------------- driver

class FakeSession:
    """Scripted node: renders --info/--pos from a mutable state, applies the prepare commands, records every command."""

    def __init__(self, port, node_state, log, call="DK5EN-14", nrf=False, apply_writes=True):
        self.port, self.s, self.log, self.call, self.nrf = port, node_state, log, call, nrf
        self.apply_writes = apply_writes
        self.lines = []
        self.closed = False

    def send(self, cmd):
        idx = len(self.lines)
        self.log.append((self.port, cmd))
        if cmd == "--info":
            self.lines += info_text(self.call, tuple(self.s["groups"]), self.s["debug"], self.s["separator"],
                                    nrf=self.nrf).split("\n")
        elif cmd == "--pos":
            self.lines += pos_text(self.s["gps"], self.s["track"]).split("\n")
        elif self.apply_writes:
            if cmd.startswith("--setgrc "):
                self.s["groups"] = [int(x) for x in cmd.split(" ", 1)[1].split(";") if x]
            elif cmd in ("--debug on", "--debug off"):
                self.s["debug"] = cmd.endswith("on")
            elif cmd in ("--debug csv", "--debug man"):
                self.s["separator"] = cmd.split()[1]
            elif cmd in ("--gps on", "--gps off"):
                self.s["gps"] = cmd.endswith("on")
            elif cmd in ("--track on", "--track off"):
                self.s["track"] = cmd.endswith("on")
        return idx

    def pump(self, seconds):
        return True

    def wait_for(self, pattern, timeout, since=0):
        return None

    def lines_since(self, idx):
        return self.lines[idx:]

    def close(self):
        self.closed = True


def drive(tmp_path, node, node_state, extra=(), **fake_kw):
    log, made = [], []

    def factory(port, dtr, log_dir):
        s = FakeSession(port, node_state, log, **fake_kw)
        made.append(s)
        return s

    out = tmp_path / "prepare.json"
    rc = pn.main(["--port", "/p/x", "--node", node, "--out", str(out), *extra],
                 session_factory=factory, fleet=FLEET)
    writes = [c for _, c in log if c not in ("--info", "--pos")]
    return rc, writes, made, json.loads(out.read_text()) if out.exists() else None


def test_driver_correct_state_writes_nothing(tmp_path, capsys):
    st = {"groups": list(GOOD_GROUPS), "debug": True, "separator": "csv", "gps": None, "track": None}
    rc, writes, made, res = drive(tmp_path, "t-deck-14", st)
    assert rc == 0 and writes == []
    assert res["ok"] is True and res["changes"] == []
    assert made[0].closed
    assert "prepare t-deck-14: 0 change(s), state ok" in capsys.readouterr().out


def test_driver_tdeck_wrong_state_writes_then_verifies(tmp_path, capsys):
    st = {"groups": [20, 232, 262, 26244], "debug": False, "separator": "man", "gps": None, "track": None}
    rc, writes, made, res = drive(tmp_path, "t-deck-14", st)
    assert rc == 0
    assert writes == ["--setgrc 9;20;232;262", "--debug on", "--debug csv"]
    assert st["groups"] == list(GOOD_GROUPS)
    assert res["ok"] is True and res["changes"] == writes
    assert res["before"]["debug"] is False and res["after"]["debug"] is True
    out = capsys.readouterr().out.splitlines()
    assert "prepare t-deck-14: --setgrc 9;20;232;262" in out
    assert "prepare t-deck-14: --debug on" in out and "prepare t-deck-14: --debug csv" in out
    assert out[-2] == "prepare t-deck-14: 3 change(s), state ok"      # the last line is "wrote <json>"
    # --info was read again after the writes
    assert made[0].lines and sum(1 for l in made[0].lines if l.startswith("...Call:")) == 2


def test_driver_tbeam_reads_pos_and_fixes_gps_and_track(tmp_path):
    st = {"groups": list(GOOD_GROUPS), "debug": False, "separator": "csv", "gps": False, "track": True}
    rc, writes, _, res = drive(tmp_path, "t-beam-92", st, call="DK5EN-92")
    assert rc == 0 and writes == ["--gps on", "--track off"]
    assert st["gps"] is True and st["track"] is False


def test_driver_tdeck_never_asks_for_pos(tmp_path):
    st = {"groups": list(GOOD_GROUPS), "debug": True, "separator": "csv", "gps": None, "track": None}
    log = []

    def factory(port, dtr, log_dir):
        return FakeSession(port, st, log, call="DK5EN-14")

    rc = pn.main(["--port", "/p/t", "--node", "t-deck-14"], session_factory=factory, fleet=FLEET)
    assert rc == 0
    assert [c for _, c in log] == ["--info"]


def test_driver_dry_run_writes_nothing_even_when_state_is_wrong(tmp_path, capsys):
    st = {"groups": [], "debug": True, "separator": "man", "gps": None, "track": None}
    rc, writes, made, res = drive(tmp_path, "t-deck-14", st, extra=("--dry-run",))
    assert rc == 0 and writes == []
    assert res["dry_run"] is True and res["planned"] == ["--setgrc 9;20;232;262", "--debug csv"]
    assert st["groups"] == [] and st["separator"] == "man"
    out = capsys.readouterr().out
    assert "prepare t-deck-14: plan --setgrc 9;20;232;262" in out
    assert "prepare t-deck-14: dry-run, 2 change(s) pending" in out
    assert made[0].closed


def test_driver_state_still_wrong_after_writes_exits_1(tmp_path, capsys):
    st = {"groups": [], "debug": False, "separator": "man", "gps": None, "track": None}
    rc, writes, _, res = drive(tmp_path, "t-deck-14", st, apply_writes=False)
    assert rc == 1
    assert writes == ["--setgrc 9;20;232;262", "--debug on", "--debug csv"]
    assert res["ok"] is False and "still wrong" in res["error"]
    assert "--setgrc 9;20;232;262" in res["error"]
    assert "FAIL" in capsys.readouterr().err


@pytest.mark.parametrize("call,node", [
    ("XX0XXX-00", "t-deck-14"),       # placeholder callsign
    ("DK5EN-92", "t-deck-14"),        # right node class, wrong registry entry
])
def test_driver_guard_refusal_sends_nothing_but_info(tmp_path, capsys, call, node):
    st = {"groups": [], "debug": False, "separator": "man", "gps": None, "track": None}
    rc, writes, made, res = drive(tmp_path, node, st, call=call)
    assert rc == 1 and writes == []
    assert res["ok"] is False and "refused" in res["error"]
    assert st["groups"] == []
    assert made[0].closed
    assert "FAIL" in capsys.readouterr().err


def test_driver_guard_refuses_a_high_power_node_before_any_write(tmp_path):
    log = []
    st = {"groups": [], "debug": False, "separator": "man", "gps": None, "track": None}

    class Hot(FakeSession):
        def send(self, cmd):
            if cmd == "--info":
                idx = len(self.lines)
                self.log.append((self.port, cmd))
                self.lines += info_text("DK5EN-14", power=20).split("\n")
                return idx
            return super().send(cmd)

    rc = pn.main(["--port", "/p/x", "--node", "t-deck-14"],
                 session_factory=lambda p, d, l: Hot(p, st, log), fleet=FLEET)
    assert rc == 1
    assert [c for _, c in log] == ["--info"]


def test_driver_unregistered_node_is_refused_without_opening_a_port(tmp_path, capsys):
    def factory(*a):
        raise AssertionError("port must not be opened")

    rc = pn.main(["--port", "/p/x", "--node", "nope-1"], session_factory=factory, fleet=FLEET)
    assert rc == 1
    assert "not registered" in capsys.readouterr().err


def test_driver_requires_port_and_node():
    with pytest.raises(SystemExit) as e:
        pn.main(["--node", "t-deck-14"], fleet=FLEET)
    assert e.value.code == 2


def test_wait_boot_continues_at_once_without_boot_text_and_waits_for_ready_with_it():
    class S:
        ready_line = ""

        def __init__(self, lines):
            self.lines, self.waited = lines, []

        def pump(self, seconds):
            return True

        def lines_since(self, idx):
            return self.lines[idx:]

        def wait_for(self, pattern, timeout, since=0):
            self.waited.append((pattern, timeout))
            if "ready" in pattern and self.ready_line:
                return re.search(pattern, self.ready_line)
            return None

    quiet = S([])
    assert pn.wait_boot(quiet) is False and quiet.waited == []
    booting = S(["CLIENT STARTED"])
    booting.ready_line = "[BOOT];ready;ms;7918;ip;1"
    assert pn.wait_boot(booting) is True
    assert [t for _, t in booting.waited] == [45.0]          # ip;1: no extra WiFi wait
    no_ip = S(["CLIENT STARTED"])
    no_ip.ready_line = "[BOOT];ready;ms;7918;ip;0"
    assert pn.wait_boot(no_ip) is True
    assert [t for _, t in no_ip.waited] == [45.0, 30.0]      # ip;0: wait for got_ip


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-q"]))
