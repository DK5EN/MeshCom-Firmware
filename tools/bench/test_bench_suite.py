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
    assert names(steps) == ["identity rak-90", "prepare rak-90", "rak harness rak-90"]
    assert steps[0].gate and steps[1].gate and not steps[2].gate
    assert "--peer-port" not in steps[2].argv
    assert "--port" in steps[2].argv and steps[2].argv[steps[2].argv.index("--port") + 1] == "/dev/tty.rak"
    assert steps[2].argv[-2:] == ["--node", "rak-90"]


def test_plan_rak_with_esp32_peer_sets_peer_port_and_ota_for_wifi_node():
    attached = {"rak-90": "/dev/tty.rak", "t-beam-92": "/dev/tty.tbeam"}
    steps = bs.plan(FLEET, attached, out=Path("/o"))
    assert names(steps) == [
        "identity rak-90", "prepare rak-90", "rak harness rak-90",
        "identity t-beam-92", "prepare t-beam-92", "oled harness t-beam-92", "wait web t-beam-92",
        "ota regression t-beam-92", "wait web t-beam-92 (after ota)", "webgui badge t-beam-92",
        "mesh exchange",
    ]
    rak = steps[2].argv
    assert rak[rak.index("--peer-port") + 1] == "/dev/tty.tbeam"
    ota = next(st for st in steps if st.name == "ota regression t-beam-92").argv
    assert ota[ota.index("--ip") + 1] == "192.168.68.73"
    assert ota[ota.index("--env") + 1] == "ttgo_tbeam"
    assert "--settings-check" in ota
    assert steps[5].argv[-1] == "/o/t-beam-92-harness.json"


def test_plan_tdeck_uses_tdeck_harness_and_skips_ota_without_host():
    fleet = {"nodes": {"t-deck-14": {**FLEET["nodes"]["t-deck-14"], "usb_serial": "X"}}}
    steps = bs.plan(fleet, {"t-deck-14": "/dev/tty.td"})
    assert names(steps) == ["identity t-deck-14", "prepare t-deck-14", "tdeck harness t-deck-14"]


def test_plan_no_ota_flag_and_unattached_nodes_produce_nothing():
    steps = bs.plan(FLEET, {"t-beam-92": "/dev/tty.tbeam"}, ota=False, badge=False)
    assert names(steps) == ["identity t-beam-92", "prepare t-beam-92", "oled harness t-beam-92"]
    assert bs.plan(FLEET, {}) == []


def test_plan_flash_esp32_bridge_uses_esptool_460800_app_only():
    steps = bs.plan(FLEET, {"t-beam-92": "/dev/tty.tbeam"}, flash=True, ota=False, badge=False)
    assert names(steps) == ["identity t-beam-92", "build ttgo_tbeam", "flash ttgo_tbeam",
                            "prepare t-beam-92", "oled harness t-beam-92"]
    up = steps[2].argv
    assert up[:4] == ["pio", "pkg", "exec", "-p"]
    assert "460800" in up and "0xC0000" in up
    assert up[-1] == ".pio/build/ttgo_tbeam/firmware.bin"
    assert "0x1000" not in up and "safeboot" not in " ".join(up)
    assert steps[1].gate and steps[2].gate


def test_plan_flash_rak_via_pio_upload_tdeck_via_esptool_app_only():
    fleet = {"nodes": {"rak-90": FLEET["nodes"]["rak-90"],
                       "t-deck-14": {**FLEET["nodes"]["t-deck-14"], "usb_serial": "X"}}}
    steps = bs.plan(fleet, {"rak-90": "/dev/tty.rak", "t-deck-14": "/dev/tty.td"}, flash=True)
    flashes = {s.name: s.argv for s in steps if s.name.startswith("flash")}
    assert flashes["flash wiscore_rak4631"][-2:] == ["--upload-port", "/dev/tty.rak"]
    td = flashes["flash t_deck_plus"]
    assert td[:4] == ["pio", "pkg", "exec", "-p"] and "0xC0000" in td and td[-1].endswith("t_deck_plus/firmware.bin")
    assert td[td.index("--port") + 1] == "/dev/tty.td" and "--target" not in td


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


def test_plan_badge_for_esp32_with_host_after_ota_before_deepsleep():
    steps = bs.plan(FLEET, {"heltec-1": "/dev/tty.h"}, deepsleep=True)
    assert names(steps) == ["identity heltec-1", "prepare heltec-1", "oled harness heltec-1", "wait web heltec-1",
                            "ota regression heltec-1", "wait web heltec-1 (after ota)",
                            "webgui badge heltec-1", "deepsleep heltec-1"]
    assert steps[3].argv[1:3] == [str(bs.BENCH / "wait_http.py"), "http://192.168.68.62/"]
    assert steps[7].argv[-2:] == ["--node", "heltec-1"]
    badge = steps[6]
    assert badge.argv == ["node", "--insecure-http-parser", "tools/webgui_badge_test.js",
                          "http://192.168.68.62/"]
    assert badge.env["NODE_PATH"].endswith("/node_modules")
    assert not badge.gate


def test_plan_badge_node_path_honours_jsdom_dir_env(monkeypatch):
    monkeypatch.setenv("MESHCOM_JSDOM_DIR", "/j")
    steps = bs.plan(FLEET, {"t-beam-92": "/dev/tty.tb"})
    assert steps[-1].env == {"NODE_PATH": "/j/node_modules"}


def test_plan_badge_absent_for_rak_and_for_nodes_without_host_or_with_flag():
    assert not any("badge" in n for n in names(bs.plan(FLEET, {"rak-90": "/dev/tty.rak"})))
    fleet = {"nodes": {"t-deck-14": {**FLEET["nodes"]["t-deck-14"], "usb_serial": "X"}}}
    assert not any("badge" in n for n in names(bs.plan(fleet, {"t-deck-14": "/dev/tty.td"})))
    assert not any("badge" in n for n in names(bs.plan(FLEET, {"t-beam-92": "/dev/tty.tb"}, badge=False)))


def test_plan_badge_still_runs_with_no_ota():
    steps = bs.plan(FLEET, {"t-beam-92": "/dev/tty.tb"}, ota=False)
    assert names(steps)[-1] == "webgui badge t-beam-92"


def test_plan_mesh_exchange_with_two_nodes_is_last_ungated_and_lists_every_port():
    attached = {"t-beam-92": "/dev/tty.tb", "rak-90": "/dev/tty.rak", "heltec-1": "/dev/tty.h"}
    steps = bs.plan(FLEET, attached, out=Path("/o"))
    mesh = steps[-1]
    assert mesh.name == "mesh exchange" and mesh.node == "fleet" and not mesh.gate
    assert mesh.timeout_s == 10 * 60
    assert mesh.argv[1].endswith("tools/bench/mesh_exchange.py")
    assert mesh.argv[mesh.argv.index("--out") + 1] == "/o/mesh-exchange.json"
    ports = [mesh.argv[i + 1] for i, a in enumerate(mesh.argv) if a == "--port"]
    assert ports == ["/dev/tty.h", "/dev/tty.rak", "/dev/tty.tb"]     # sorted by node name


def test_plan_mesh_exchange_absent_with_one_node_or_no_mesh_flag():
    assert "mesh exchange" not in names(bs.plan(FLEET, {"rak-90": "/dev/tty.rak"}))
    two = {"rak-90": "/dev/tty.rak", "t-beam-92": "/dev/tty.tb"}
    assert "mesh exchange" in names(bs.plan(FLEET, two))
    assert "mesh exchange" not in names(bs.plan(FLEET, two, mesh=False))


def test_plan_prepare_runs_right_after_flash_and_before_every_harness():
    attached = {"rak-90": "/dev/tty.rak", "t-beam-92": "/dev/tty.tb", "heltec-1": "/dev/tty.h"}
    steps = bs.plan(FLEET, attached, flash=True, out=Path("/o"))
    ns = names(steps)
    harness = {"rak-90": "rak harness rak-90", "t-beam-92": "oled harness t-beam-92",
               "heltec-1": "oled harness heltec-1"}
    flash = {"rak-90": "flash wiscore_rak4631", "t-beam-92": "flash ttgo_tbeam",
             "heltec-1": "flash heltec_wifi_lora_32_V3"}
    for node, h in harness.items():
        assert ns.index(flash[node]) + 1 == ns.index(f"prepare {node}") == ns.index(h) - 1
    prep = next(s for s in steps if s.name == "prepare t-beam-92")
    assert prep.gate and prep.node == "t-beam-92"
    assert prep.argv == [sys.executable, str(bs.BENCH / "prepare_node.py"),
                         "--port", "/dev/tty.tb", "--node", "t-beam-92"]
    # without --flash it follows the identity step directly, and without identity steps it is first
    assert names(bs.plan(FLEET, {"rak-90": "/dev/tty.rak"}))[:2] == ["identity rak-90", "prepare rak-90"]
    assert names(bs.plan(FLEET, {"rak-90": "/dev/tty.rak"}, identity_steps=False))[0] == "prepare rak-90"


def test_plan_no_prepare_drops_the_step_and_main_flag_threads_through(tmp_path, monkeypatch, capsys):
    attached = {"rak-90": "/dev/tty.rak", "t-beam-92": "/dev/tty.tb"}
    assert not any(n.startswith("prepare") for n in names(bs.plan(FLEET, attached, prepare=False)))
    fleet_file = tmp_path / "fleet.json"
    fleet_file.write_text(json.dumps(FLEET))
    monkeypatch.setattr(bs, "list_comports",
                        lambda: [port("/dev/tty.rak", "230D6EBB3266D20E"), port("/dev/tty.tb", "573C000584")])
    assert bs.main(["--dry-run", "--fleet", str(fleet_file)]) == 0
    assert "prepare rak-90" in capsys.readouterr().out
    assert bs.main(["--dry-run", "--no-prepare", "--fleet", str(fleet_file)]) == 0
    assert "prepare " not in capsys.readouterr().out


# ---------------------------------------------------------------- runner

class FakeRun:
    """subprocess.run stand-in: exit code by step name, records calls."""

    def __init__(self, rc_by_token):
        self.rc_by_token = rc_by_token
        self.calls = []
        self.envs = []

    def __call__(self, argv, cwd, stdout, stderr, timeout, env=None):
        self.calls.append(list(argv))
        self.envs.append(env)
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
    assert by_name["prepare rak-90"].status == "SKIP" and by_name["rak harness rak-90"].status == "SKIP"
    assert "gate" in by_name["rak harness rak-90"].detail
    assert by_name["identity t-beam-92"].status == "OK" and by_name["prepare t-beam-92"].status == "OK"
    assert by_name["oled harness t-beam-92"].status == "OK"
    assert by_name["ota regression t-beam-92"].status == "OK"
    # the skipped harness was never launched
    assert not any("rak_harness.py" in " ".join(c) for c in fake.calls)
    # one log per executed step, first line is the command
    logs = sorted(tmp_path.glob("stage3-*.log"))
    assert len(logs) == 9      # identity x2, prepare, oled, wait, ota, wait, badge, mesh; the skipped rak prepare/harness have none
    assert by_name["webgui badge t-beam-92"].status == "OK" and by_name["mesh exchange"].status == "OK"
    assert logs[0].read_text().startswith("$ ")


def test_runner_non_gate_failure_does_not_skip_later_steps(tmp_path):
    steps = bs.plan(FLEET, {"t-beam-92": "/dev/tty.tbeam"}, out=tmp_path)
    fake = FakeRun({"oled_harness.py": 1})
    results = bs.run_steps(steps, tmp_path, runner=fake, echo=lambda s: None)
    assert [r.status for r in results] == ["OK", "OK", "FAIL", "OK", "OK", "OK", "OK"]    # identity, prepare, oled, wait, ota, wait, badge


def test_runner_merges_step_env_over_os_environ_only_when_set(tmp_path, monkeypatch):
    monkeypatch.setenv("MESHCOM_MARKER", "kept")
    steps = bs.plan(FLEET, {"t-beam-92": "/dev/tty.tbeam"}, out=tmp_path)
    fake = FakeRun({})
    bs.run_steps(steps, tmp_path, runner=fake, echo=lambda s: None)
    by_name = dict(zip(names(steps), fake.envs))
    assert by_name["identity t-beam-92"] is None
    env = by_name["webgui badge t-beam-92"]
    assert env["NODE_PATH"].endswith("/node_modules") and env["MESHCOM_MARKER"] == "kept"


def test_main_flags_no_badge_and_no_mesh_thread_through_plan(tmp_path, monkeypatch, capsys):
    fleet_file = tmp_path / "fleet.json"
    fleet_file.write_text(json.dumps(FLEET))
    ports = [port("/dev/tty.rak", "230D6EBB3266D20E"), port("/dev/tty.tb", "573C000584")]
    monkeypatch.setattr(bs, "list_comports", lambda: ports)
    assert bs.main(["--dry-run", "--fleet", str(fleet_file)]) == 0
    full = capsys.readouterr().out
    assert "webgui badge t-beam-92" in full and "mesh exchange" in full
    assert bs.main(["--dry-run", "--no-badge", "--no-mesh", "--fleet", str(fleet_file)]) == 0
    slim = capsys.readouterr().out
    assert "webgui badge" not in slim and "mesh exchange" not in slim


def test_runner_timeout_is_a_failure_with_detail(tmp_path):
    steps = bs.plan(FLEET, {"t-beam-92": "/dev/tty.tbeam"}, ota=False, out=tmp_path)
    fake = FakeRun({"oled_harness.py": "timeout"})
    results = bs.run_steps(steps, tmp_path, runner=fake, echo=lambda s: None)
    assert results[2].status == "FAIL" and results[2].rc == -1      # identity, prepare, oled
    assert "timeout" in results[2].detail


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


def test_harness_and_ota_steps_pass_the_fleet_node_name():
    fleet = {"nodes": {"t-deck-14": {**FLEET["nodes"]["t-deck-14"], "usb_serial": "X", "host": "10.0.0.5"}}}
    steps = bs.plan(fleet, {"t-deck-14": "/dev/tty.td"}, badge=False)
    by = {s.name: s.argv for s in steps}
    assert by["tdeck harness t-deck-14"][-2:] == ["--node", "t-deck-14"]
    assert by["ota regression t-deck-14"][-2:] == ["--node", "t-deck-14"]
    oled = bs.plan(FLEET, {"t-beam-92": "/dev/tty.tb"}, ota=False, badge=False)
    oled_argv = next(s.argv for s in oled if s.name == "oled harness t-beam-92")
    assert "--node" not in oled_argv          # oled_harness.py has no identity-guard flag
    assert by["prepare t-deck-14"][-2:] == ["--node", "t-deck-14"]


def test_instrument_flash_builds_with_the_no_space_flag_and_verifies_the_elf():
    steps = bs.plan(FLEET, {"t-beam-92": "/dev/tty.tb"}, flash=True, instrument=True,
                    ota=False, badge=False)
    assert names(steps)[:4] == ["identity t-beam-92", "build ttgo_tbeam",
                                "verify instrument ttgo_tbeam", "flash ttgo_tbeam"]
    build, verify = steps[1], steps[2]
    assert build.env == {"PLATFORMIO_BUILD_FLAGS": "-DINSTRUMENT_ENABLED=1"}
    assert " " not in build.env["PLATFORMIO_BUILD_FLAGS"]
    assert verify.gate and verify.argv[0] == "sh"
    assert ".pio/build/ttgo_tbeam/firmware.elf" in verify.argv[2] and "SRVIP" in verify.argv[2]
    plain = bs.plan(FLEET, {"t-beam-92": "/dev/tty.tb"}, flash=True, ota=False, badge=False)
    assert "verify instrument ttgo_tbeam" not in names(plain) and plain[1].env == {}


def test_wait_http_wait_for_returns_elapsed_or_none():
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import wait_http as wh
    clock = [0.0]
    answers = iter([False, False, True])
    waited = wh.wait_for(lambda: next(answers), timeout_s=100, interval_s=3,
                         now=lambda: clock[0], sleep=lambda s: clock.__setitem__(0, clock[0] + s))
    assert waited == 6
    clock[0] = 0.0
    assert wh.wait_for(lambda: False, timeout_s=10, interval_s=4,
                       now=lambda: clock[0], sleep=lambda s: clock.__setitem__(0, clock[0] + s)) is None


def test_probe_hosts_overrides_fleet_host_from_identity_output():
    fleet = {"nodes": {"t-beam-92": dict(FLEET["nodes"]["t-beam-92"]), "rak-90": dict(FLEET["nodes"]["rak-90"])}}
    outputs = {"t-beam-92": "t-beam-92: ok -- DK5EN-92, 2 dBm, ip 192.168.68.70, web on, clock NTP\n",
               "rak-90": "rak-90: ok -- DK5EN-90, 2 dBm, ip -, web off, clock NTP\n"}

    def fake_run(argv, cwd, capture_output, text, timeout):
        node = argv[argv.index("--node") + 1]
        return SimpleNamespace(stdout=outputs[node], stderr="", returncode=0)
    msgs = []
    live, failed = bs.probe_hosts({"t-beam-92": "/dev/a", "rak-90": "/dev/b"}, fleet, runner=fake_run, echo=msgs.append)
    assert live == {"t-beam-92": "192.168.68.70"} and failed == {}
    assert fleet["nodes"]["t-beam-92"]["host"] == "192.168.68.70"
    assert fleet["nodes"]["rak-90"]["host"] is None
    assert any("192.168.68.73 -> 192.168.68.70" in m for m in msgs)


def test_probe_hosts_reports_a_refused_node_and_plan_can_omit_identity_steps(tmp_path):
    fleet = {"nodes": {"heltec-1": dict(FLEET["nodes"]["heltec-1"])}}

    def refused(argv, cwd, capture_output, text, timeout):
        return SimpleNamespace(stdout="heltec-1: refused -- no '...Call: <...>' line in --info\n", stderr="", returncode=1)
    live, failed = bs.probe_hosts({"heltec-1": "/dev/h"}, fleet, runner=refused, echo=lambda s: None)
    assert live == {} and "refused" in failed["heltec-1"]
    steps = bs.plan(FLEET, {"t-beam-92": "/dev/tty.tb"}, ota=False, badge=False, identity_steps=False)
    assert names(steps) == ["prepare t-beam-92", "oled harness t-beam-92"]
    # persist_hosts writes only changed host fields
    f = tmp_path / "fleet.json"
    f.write_text(json.dumps({"nodes": {"t-beam-92": {"host": "1.1.1.1", "board": "ttgo_tbeam"}}}))
    assert bs.persist_hosts({"nodes": {"t-beam-92": {"host": "2.2.2.2"}}}, f) is True
    assert json.loads(f.read_text())["nodes"]["t-beam-92"] == {"host": "2.2.2.2", "board": "ttgo_tbeam"}
    assert bs.persist_hosts({"nodes": {"t-beam-92": {"host": "2.2.2.2"}}}, f) is False


def test_runner_maps_exit_3_to_skip(tmp_path):
    steps = bs.plan(FLEET, {"t-beam-92": "/dev/tty.tbeam"}, ota=False, out=tmp_path, identity_steps=False)
    fake = FakeRun({"webgui_badge_test.js": 3})
    results = bs.run_steps(steps, tmp_path, runner=fake, echo=lambda s: None)
    by = {r.name: r for r in results}
    assert by["webgui badge t-beam-92"].status == "SKIP" and by["webgui badge t-beam-92"].rc == 3
    assert by["oled harness t-beam-92"].status == "OK"


def test_instrument_flag_reaches_the_rak_upload_step_too():
    steps = bs.plan(FLEET, {"rak-90": "/dev/tty.rak"}, flash=True, instrument=True, mesh=False)
    by = {s.name: s for s in steps}
    assert by["build wiscore_rak4631"].env == {"PLATFORMIO_BUILD_FLAGS": "-DINSTRUMENT_ENABLED=1"}
    assert by["flash wiscore_rak4631"].env == {"PLATFORMIO_BUILD_FLAGS": "-DINSTRUMENT_ENABLED=1"}
    plain = {s.name: s for s in bs.plan(FLEET, {"rak-90": "/dev/tty.rak"}, flash=True, mesh=False)}
    assert plain["flash wiscore_rak4631"].env == {}


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-q"]))
