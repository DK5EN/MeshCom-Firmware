"""Host tests for tools/bench/bench_suite.py -- the pure parts (port discovery,
plan, summary) plus the runner against a fake subprocess. No hardware."""
from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path
from types import SimpleNamespace

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bench_suite as bs  # noqa: E402

FLEET = {
    "nodes": {
        "heltec-1": {"call": "DK5EN-1", "board": "heltec_wifi_lora_32_V3", "family": "esp32",
                     "usb_serial": "0001", "host": "192.168.68.62"},
        "t-deck-14": {"call": "DK5EN-14", "board": "t_deck_plus", "family": "esp32",
                      "usb_serial": None, "host": None},
        "t-beam-92": {"call": "DK5EN-92", "board": "ttgo_tbeam", "family": "esp32",
                      "usb_serial": "573C000584", "host": "192.168.68.73"},
        "rak-90": {"call": "DK5EN-90", "board": "wiscore_rak4631", "family": "nrf52",
                   "usb_serial": "230D6EBB3266D20E", "host": None},
    }
}


def port(device, serial_number):
    return SimpleNamespace(device=device, serial_number=serial_number)


def names(steps):
    return [s.name for s in steps]


# ---------------------------------------------------------------- discovery

def test_discover_matches_by_usb_serial_not_port_name():
    ports = [port("/dev/cu.usbmodem1101", "230D6EBB3266D20E"),
             port("/dev/cu.usbserial-573C0005841", "573C000584"),
             port("/dev/cu.Bluetooth-Incoming-Port", None)]
    assert bs.discover_ports(FLEET, ports) == {
        "rak-90": "/dev/cu.usbmodem1101",
        "t-beam-92": "/dev/cu.usbserial-573C0005841",
    }


def test_discover_ignores_nodes_without_usb_serial_and_unknown_ports():
    ports = [port("/dev/cu.usbmodem2101", "E072A1AD65E0"), port("/dev/cu.debug-console", None)]
    assert bs.discover_ports(FLEET, ports) == {}


def test_real_fleet_file_loads_and_has_usb_serials():
    fleet = bs.load_fleet()
    assert any(n.get("usb_serial") for n in fleet["nodes"].values())


# ---------------------------------------------------------------- plan

def test_plan_rak_alone_identity_then_harness_no_peer_no_ota():
    steps = bs.plan(FLEET, {"rak-90": "/dev/tty.rak"})
    assert names(steps) == ["identity rak-90", "rak harness rak-90"]
    assert steps[0].gate and not steps[1].gate
    assert "--peer-port" not in steps[1].argv
    assert steps[1].argv[-4:-2] == ["--port", "/dev/tty.rak"]


def test_plan_rak_with_esp32_peer_sets_peer_port_and_ota_for_wifi_node():
    attached = {"rak-90": "/dev/tty.rak", "t-beam-92": "/dev/tty.tbeam"}
    steps = bs.plan(FLEET, attached, out=Path("/o"))
    assert names(steps) == [
        "identity rak-90", "rak harness rak-90",
        "identity t-beam-92", "oled harness t-beam-92", "ota regression t-beam-92",
    ]
    rak = steps[1].argv
    assert rak[rak.index("--peer-port") + 1] == "/dev/tty.tbeam"
    ota = steps[4].argv
    assert ota[ota.index("--ip") + 1] == "192.168.68.73"
    assert ota[ota.index("--env") + 1] == "ttgo_tbeam"
    assert "--settings-check" in ota
    assert steps[3].argv[-1] == "/o/t-beam-92-harness.json"


def test_plan_tdeck_uses_tdeck_harness_and_skips_ota_without_host():
    fleet = {"nodes": {"t-deck-14": {**FLEET["nodes"]["t-deck-14"], "usb_serial": "X"}}}
    steps = bs.plan(fleet, {"t-deck-14": "/dev/tty.td"})
    assert names(steps) == ["identity t-deck-14", "tdeck harness t-deck-14"]


def test_plan_no_ota_flag_and_unattached_nodes_produce_nothing():
    steps = bs.plan(FLEET, {"t-beam-92": "/dev/tty.tbeam"}, ota=False)
    assert names(steps) == ["identity t-beam-92", "oled harness t-beam-92"]
    assert bs.plan(FLEET, {}) == []


def test_plan_flash_esp32_bridge_uses_esptool_460800_app_only():
    steps = bs.plan(FLEET, {"t-beam-92": "/dev/tty.tbeam"}, flash=True, ota=False)
    assert names(steps) == ["identity t-beam-92", "build ttgo_tbeam", "flash ttgo_tbeam",
                            "oled harness t-beam-92"]
    up = steps[2].argv
    assert up[:4] == ["pio", "pkg", "exec", "-p"]
    assert "460800" in up and "0xC0000" in up
    assert up[-1] == ".pio/build/ttgo_tbeam/firmware.bin"
    assert "0x1000" not in up and "safeboot" not in " ".join(up)
    assert steps[1].gate and steps[2].gate


def test_plan_flash_rak_and_tdeck_go_through_pio_upload_with_explicit_port():
    fleet = {"nodes": {"rak-90": FLEET["nodes"]["rak-90"],
                       "t-deck-14": {**FLEET["nodes"]["t-deck-14"], "usb_serial": "X"}}}
    steps = bs.plan(fleet, {"rak-90": "/dev/tty.rak", "t-deck-14": "/dev/tty.td"}, flash=True)
    flashes = {s.name: s.argv for s in steps if s.name.startswith("flash")}
    assert flashes["flash wiscore_rak4631"][-2:] == ["--upload-port", "/dev/tty.rak"]
    assert flashes["flash t_deck_plus"][-2:] == ["--upload-port", "/dev/tty.td"]
    assert "--target" in flashes["flash t_deck_plus"]


def test_plan_extudp_and_deepsleep_are_opt_in_and_board_specific():
    attached = {"rak-90": "/dev/tty.rak", "heltec-1": "/dev/tty.h", "t-beam-92": "/dev/tty.tb"}
    steps = bs.plan(FLEET, attached, ota=False, extudp=True, deepsleep=True, soak_seconds=120)
    assert "rak extudp rak-90" in names(steps)
    assert "deepsleep heltec-1" in names(steps)
    assert "deepsleep t-beam-92" not in names(steps)
    ext = next(s for s in steps if s.name == "rak extudp rak-90")
    assert ext.argv[ext.argv.index("--soak-seconds") + 1] == "120"
    assert ext.timeout_s > 120
    plain = bs.plan(FLEET, attached, ota=False)
    assert "rak extudp rak-90" not in names(plain) and "deepsleep heltec-1" not in names(plain)


# ---------------------------------------------------------------- runner

class FakeRun:
    """subprocess.run stand-in: exit code by step name, records calls."""

    def __init__(self, rc_by_token):
        self.rc_by_token = rc_by_token
        self.calls = []

    def __call__(self, argv, cwd, stdout, stderr, timeout):
        self.calls.append(list(argv))
        stdout.write("fake output\n")
        rc = 0
        for key, code in self.rc_by_token.items():
            toks = key if isinstance(key, tuple) else (key,)
            if all(any(t in a for a in argv) for t in toks):
                rc = code
        if rc == "timeout":
            raise subprocess.TimeoutExpired(argv, timeout)
        return SimpleNamespace(returncode=rc)


def test_runner_skips_a_node_after_its_gate_fails_but_continues_others(tmp_path):
    attached = {"rak-90": "/dev/tty.rak", "t-beam-92": "/dev/tty.tbeam"}
    steps = bs.plan(FLEET, attached, out=tmp_path)
    fake = FakeRun({("identity_guard.py", "rak-90"): 1})
    echoed = []
    results = bs.run_steps(steps, tmp_path, runner=fake, echo=echoed.append)
    by_name = {r.name: r for r in results}
    assert by_name["identity rak-90"].status == "FAIL"
    assert by_name["rak harness rak-90"].status == "SKIP"
    assert "gate" in by_name["rak harness rak-90"].detail
    assert by_name["identity t-beam-92"].status == "OK"
    assert by_name["oled harness t-beam-92"].status == "OK"
    assert by_name["ota regression t-beam-92"].status == "OK"
    # the skipped harness was never launched
    assert not any("rak_harness.py" in " ".join(c) for c in fake.calls)
    # one log per executed step, first line is the command
    logs = sorted(tmp_path.glob("stage3-*.log"))
    assert len(logs) == 4
    assert logs[0].read_text().startswith("$ ")


def test_runner_non_gate_failure_does_not_skip_later_steps(tmp_path):
    steps = bs.plan(FLEET, {"t-beam-92": "/dev/tty.tbeam"}, out=tmp_path)
    fake = FakeRun({"oled_harness.py": 1})
    results = bs.run_steps(steps, tmp_path, runner=fake, echo=lambda s: None)
    assert [r.status for r in results] == ["OK", "FAIL", "OK"]


def test_runner_timeout_is_a_failure_with_detail(tmp_path):
    steps = bs.plan(FLEET, {"t-beam-92": "/dev/tty.tbeam"}, ota=False, out=tmp_path)
    fake = FakeRun({"oled_harness.py": "timeout"})
    results = bs.run_steps(steps, tmp_path, runner=fake, echo=lambda s: None)
    assert results[1].status == "FAIL" and results[1].rc == -1
    assert "timeout" in results[1].detail


def test_summary_counts_and_names_absent_nodes():
    res = [bs.Result("a", "n", [], "OK"), bs.Result("b", "n", [], "FAIL"), bs.Result("c", "n", [], "SKIP")]
    txt = bs.summarize(res, {"rak-90": "/dev/tty.rak"}, ["heltec-1", "t-deck-14"])
    assert txt.startswith("1 ok, 1 failed, 1 skipped of 3 steps")
    assert "rak-90@/dev/tty.rak" in txt and "not attached: heltec-1, t-deck-14" in txt


def test_main_dry_run_with_nothing_attached_prints_plan_and_exits_zero(tmp_path, monkeypatch, capsys):
    fleet_file = tmp_path / "fleet.json"
    fleet_file.write_text(json.dumps(FLEET))
    monkeypatch.setattr(bs, "list_comports", lambda: [])
    rc = bs.main(["--dry-run", "--fleet", str(fleet_file)])
    out = capsys.readouterr().out
    assert rc == 0
    assert "nothing to run" in out and "not attached" in out


def test_main_without_hardware_writes_summary_and_fails(tmp_path, monkeypatch):
    fleet_file = tmp_path / "fleet.json"
    fleet_file.write_text(json.dumps(FLEET))
    monkeypatch.setattr(bs, "list_comports", lambda: [])
    rc = bs.main(["--fleet", str(fleet_file), "--out", str(tmp_path / "o")])
    assert rc == 1
    summary = json.loads((tmp_path / "o" / "bench-summary.json").read_text())
    assert summary["steps"] == [] and "none attached" in summary["summary"]


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-q"]))
