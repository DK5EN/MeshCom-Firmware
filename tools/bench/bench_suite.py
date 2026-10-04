#!/usr/bin/env python3
"""Stage 3 of tools/regression.sh: the bench-fleet regression, unattended.

Finds every fleet.json node that is attached over USB (matched by USB serial
number, never by port name -- port names move between plug-ins), runs the
identity guard on it, then the per-board harness, then the OTA regression on
the WiFi boards, and optionally flashes first / runs the EXTUDP and deep-sleep
regressions. Nodes that are not attached are reported and skipped, not failed:
the fleet is rarely complete on the desk.

    python3 tools/bench/bench_suite.py                 # harness + OTA on what is attached
    python3 tools/bench/bench_suite.py --dry-run       # print the plan only
    python3 tools/bench/bench_suite.py --flash         # build + flash each node first
    python3 tools/bench/bench_suite.py --extudp --deepsleep --soak-seconds 120

Every step is one subprocess with its own log under --out; the result lands
in <out>/bench-summary.json. Exit 1 if any step failed. Steps run strictly one
after another: the harnesses hold serial ports, pio must not run twice at
once, and the RAK's mheard scenario wants an ESP32 peer that is idle.

Three further steps are on by default (opt out with --no-prepare / --no-badge / --no-mesh):

  * "prepare <node>": tools/bench/prepare_node.py, right after the flash steps
    and before the harness. Idempotent: writes the bench groups 9;20;232;262 and
    the board's debug / GPS / track settings only where they differ (gate step).

  * REG-04 "webgui badge <node>": tools/webgui_badge_test.js (jsdom) against the
    live web GUI of every attached ESP32 node that has a `host` in fleet.json.
    Runs after the node's OTA step, so the node is back and reachable. jsdom is
    not a repo dependency: tools/regression.sh installs it into
    $MESHCOM_JSDOM_DIR (default ~/.cache/meshcom-jsdom) and the step points
    NODE_PATH at its node_modules. `--insecure-http-parser` is needed because
    the node's web server ends its status line with a bare LF.
  * REG-05 / TM-26 "mesh exchange": one final cross-node step when at least two
    nodes are attached. The tool tools/bench/mesh_exchange.py is written by a
    sibling task; its CLI is `--port <dev>` repeated once per attached node,
    plus `--out <json>`. This module only builds that argv.

What is deliberately NOT here: the AP-reboot run (TM-38, the bench Mac loses its own WLAN), the CDC
unplug proof (needs a hand on the cable) and the operator eye/ear checks --
see docs/automation-runner-runbook.md 2.4/2.5/2.7.
"""
from __future__ import annotations

import argparse
import json
import re
import os
import subprocess
import sys
import time
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import Any, Callable, Dict, Iterable, List, Optional

ROOT = Path(__file__).resolve().parents[2]
BENCH = Path("tools/bench")
FLEET_FILE = ROOT / BENCH / "fleet.json"

# Board env -> which harness drives it. Everything U8g2 goes through the OLED
# harness; the T-Deck Plus has its own; the RAK is the nRF52 platform.
TDECK_ENVS = {"t_deck_plus", "t_deck"}
RAK_ENVS = {"wiscore_rak4631"}
DEEPSLEEP_ENVS = {"heltec_wifi_lora_32_V3"}       # DS-03: CP2102 DTR == PRG button
HARNESS_TIMEOUT_S = 15 * 60
FLASH_TIMEOUT_S = 10 * 60
OTA_TIMEOUT_S = 10 * 60
BADGE_TIMEOUT_S = 5 * 60
MESH_TIMEOUT_S = 10 * 60
PREPARE_TIMEOUT_S = 4 * 60


@dataclass
class Step:
    name: str
    node: str
    argv: List[str]
    timeout_s: int = HARNESS_TIMEOUT_S
    gate: bool = False          # a failing gate step skips the node's later steps
    env: Dict[str, str] = field(default_factory=dict)   # merged over os.environ when non-empty


@dataclass
class Result:
    name: str
    node: str
    argv: List[str]
    status: str                 # OK | FAIL | SKIP
    rc: Optional[int] = None
    seconds: float = 0.0
    log: str = ""
    detail: str = ""


# ---------------------------------------------------------------- pure parts

def load_fleet(path: Path = FLEET_FILE) -> dict:
    return json.loads(path.read_text())


def discover_ports(fleet: dict, comports: Iterable[Any]) -> Dict[str, str]:
    """Map fleet node name -> serial device for every node whose usb_serial
    matches an attached port. `comports` is pyserial's list_ports.comports()
    (or anything with .device and .serial_number)."""
    by_serial = {}
    for p in comports:
        sn = getattr(p, "serial_number", None)
        if sn:
            by_serial[str(sn)] = str(getattr(p, "device"))
    found: Dict[str, str] = {}
    for name, node in fleet.get("nodes", {}).items():
        sn = node.get("usb_serial")
        if sn and str(sn) in by_serial:
            found[name] = by_serial[str(sn)]
    return found


INSTRUMENT_MARKER = "SRVIP\\];err"   # printed only by instrument images (src/instrument.h)


def build_env_for(instrument: bool) -> Dict[str, str]:
    """PLATFORMIO_BUILD_FLAGS for an instrument image, or nothing."""
    return {"PLATFORMIO_BUILD_FLAGS": "-DINSTRUMENT_ENABLED=1"} if instrument else {}


def build_steps(name: str, env: str, instrument: bool = False) -> List[Step]:
    """Build (optionally as an instrument image) and prove the image.

    The bench harnesses drive --oledstat/--instr/--btn and friends, which
    live behind INSTRUMENT_ENABLED in src/command_functions.cpp; a release
    image answers "wrong command" (first stage-3 run, 2026-10-03). The flag
    must be the no-space form -DINSTRUMENT_ENABLED=1 (PlatformIO splits the
    env var on whitespace) and changing PLATFORMIO_BUILD_FLAGS wipes
    .pio/build, so the next host gate rebuilds cold -- and the other way
    round: a host gate run without the flag wipes the board images, which is
    why the OTA step rebuilds its image instead of trusting .pio/build
    (run 8, 2026-10-04: "firmware not found" on both WiFi boards after a
    `--stage all` that started with stage 1). The verify step string-scans
    the ELF for an instrument-only marker before anything is flashed.
    """
    steps = [Step(f"build {env}", name, ["pio", "run", "-e", env], FLASH_TIMEOUT_S, gate=True,
                  env=build_env_for(instrument))]
    if instrument:
        steps.append(Step(f"verify instrument {env}", name,
                          ["sh", "-c", f"strings .pio/build/{env}/firmware.elf | grep -q '{INSTRUMENT_MARKER}'"],
                          60, gate=True))
    return steps


def flash_steps(name: str, env: str, dev: str, instrument: bool = False) -> List[Step]:
    """build_steps() plus the upload for the board family."""
    build_env = build_env_for(instrument)
    steps = build_steps(name, env, instrument)
    if env in RAK_ENVS:
        # Serial DFU (RAK, must be running, not in UF2 mode) goes through pio's
        # own uploader; the explicit --upload-port keeps pio's autodetect off
        # the wrong board. The upload target re-evaluates the project checksum,
        # so it MUST see the same PLATFORMIO_BUILD_FLAGS as the build step --
        # without them pio rebuilt a plain image and flashed that (run 6,
        # 2026-10-04: --instr answered "wrong command" on the RAK although the
        # verify step had passed on the instrument ELF). ESP32 boards, the
        # T-Deck included, take the esptool path below: the variant
        # upload_command would rewrite bootloader, partitions and the root
        # safeboot image, which a regression run must not touch (app partition
        # is 0xC0000 on every safeboot layout).
        up = Step(f"flash {env}", name,
                  ["pio", "run", "-e", env, "--target", "upload", "--upload-port", dev],
                  FLASH_TIMEOUT_S, gate=True, env=build_env)
    else:
        # CP2102/CH9102 bridges: esptool at 460800 (921600 fails on them),
        # app only at 0xC0000 -- the variant's upload_command would also
        # rewrite bootloader/partitions/safeboot, which a regression run must
        # not touch (docs/automation-runner-runbook.md 2.2).
        up = Step(f"flash {env}", name,
                  ["pio", "pkg", "exec", "-p", "tool-esptoolpy", "--", "esptool",
                   "--port", dev, "-b", "460800", "write_flash", "0xC0000",
                   f".pio/build/{env}/firmware.bin"],
                  FLASH_TIMEOUT_S, gate=True)
    steps.append(up)
    return steps


def jsdom_node_path() -> str:
    """NODE_PATH for the jsdom cache that tools/regression.sh installs."""
    base = os.environ.get("MESHCOM_JSDOM_DIR", os.path.expanduser("~/.cache/meshcom-jsdom"))
    return base + "/node_modules"


def plan(fleet: dict, attached: Dict[str, str], *, flash: bool = False, ota: bool = True,
         extudp: bool = False, deepsleep: bool = False, soak_seconds: int = 600,
         badge: bool = True, mesh: bool = True, instrument: bool = False,
         identity_steps: bool = True, prepare: bool = True, out: Path = Path(".")) -> List[Step]:
    """The ordered step list for the attached nodes. Pure: no I/O."""
    steps: List[Step] = []
    nodes = fleet.get("nodes", {})
    esp32_ports = [attached[n] for n in sorted(attached) if nodes[n].get("family") == "esp32"]
    py = sys.executable
    for name in sorted(attached):
        node = nodes[name]
        env = node["board"]
        dev = attached[name]
        if identity_steps:
            # main() runs the guard through probe_hosts() before planning and
            # passes identity_steps=False then: a second guard 7 s later hits
            # a node that the first one just rebooted (run 3, 2026-10-03).
            steps.append(Step(f"identity {name}", name,
                              [py, str(BENCH / "identity_guard.py"), "--node", name, "--port", dev],
                              timeout_s=60, gate=True))
        if flash:
            steps.extend(flash_steps(name, env, dev, instrument))
        if prepare:
            # prepare_node.py puts the node into the state the harnesses assume
            # (groups 9;20;232;262, debug/GPS/track per board; operator decision
            # 2026-10-04). After the flash (a flash restores defaults) and before
            # the harness; it runs its own identity guard before any write.
            steps.append(Step(f"prepare {name}", name,
                              [py, str(BENCH / "prepare_node.py"), "--port", dev, "--node", name],
                              PREPARE_TIMEOUT_S, gate=True))
        summary = str(out / f"{name}-harness.json")
        if env in TDECK_ENVS:
            steps.append(Step(f"tdeck harness {name}", name,
                              [py, str(BENCH / "tdeck_harness.py"), "--scenario", "all",
                               "--port", dev, "--out", summary, "--node", name]))
        elif env in RAK_ENVS:
            argv = [py, str(BENCH / "rak_harness.py"), "--scenario", "all",
                    "--port", dev, "--out", summary, "--node", name]
            if esp32_ports:
                argv += ["--peer-port", esp32_ports[0]]
            steps.append(Step(f"rak harness {name}", name, argv))
            if extudp:
                steps.append(Step(f"rak extudp {name}", name,
                                  [py, str(BENCH / "rak_harness.py"), "--scenario", "extudp",
                                   "--port", dev, "--out", str(out / f"{name}-extudp.json"),
                                   "--soak-seconds", str(soak_seconds), "--node", name],
                                  timeout_s=soak_seconds + 10 * 60))
        else:
            steps.append(Step(f"oled harness {name}", name,
                              [py, str(BENCH / "oled_harness.py"), "--scenario", "all",
                               "--port", dev, "--out", summary]))
        if ota and not flash and node.get("family") == "esp32" and node.get("host"):
            # The OTA step flashes .pio/build/<env>/firmware.bin; without a
            # flash step in this run that image is whatever the last pio
            # invocation left, and a host gate (stage 1) run without the
            # instrument flag wipes it (project checksum). Build it here, as
            # the flash step would, so the OTA image is the fleet's standing
            # instrument image and proven as such.
            steps.extend(build_steps(name, env, instrument))
        if (ota or badge) and node.get("family") == "esp32" and node.get("host"):
            # The harness reboots the node (port open) and leaves it mid-boot;
            # ota_regression.py runs its own identity guard first and refused
            # every node in the first stage-3 runs ("no WiFi IP yet"). Wait for
            # the web server before OTA, and again before the badge test, since
            # OTA reboots once more and the T-Beam's WiFi needs about a minute.
            steps.append(Step(f"wait web {name}", name,
                              [py, str(BENCH / "wait_http.py"), f"http://{node['host']}/",
                               "--timeout", "180"], 240))
        if ota and node.get("family") == "esp32" and node.get("host"):
            steps.append(Step(f"ota regression {name}", name,
                              [py, str(BENCH / "ota_regression.py"), "--port", dev, "--env", env,
                               "--ip", node["host"], "--settings-check", "--node", name],
                              OTA_TIMEOUT_S))
        if badge and node.get("family") == "esp32" and node.get("host"):
            if ota:
                steps.append(Step(f"wait web {name} (after ota)", name,
                                  [py, str(BENCH / "wait_http.py"), f"http://{node['host']}/",
                                   "--timeout", "180"], 240))
            steps.append(Step(f"webgui badge {name}", name,
                              ["node", "--insecure-http-parser", "tools/webgui_badge_test.js",
                               f"http://{node['host']}/"],
                              BADGE_TIMEOUT_S, env={"NODE_PATH": jsdom_node_path()}))
        if deepsleep and env in DEEPSLEEP_ENVS:
            steps.append(Step(f"deepsleep {name}", name,
                              [py, str(BENCH / "deepsleep_button.py"), "--port", dev,
                               "--node", name]))
    if mesh and len(attached) >= 2:
        argv = [py, str(BENCH / "mesh_exchange.py"), "--out", str(out / "mesh-exchange.json")]
        for n in sorted(attached):
            argv += ["--port", attached[n]]
        steps.append(Step("mesh exchange", "fleet", argv, MESH_TIMEOUT_S))
    return steps


def summarize(results: List[Result], attached: Dict[str, str], absent: List[str]) -> str:
    ok = sum(1 for r in results if r.status == "OK")
    fail = sum(1 for r in results if r.status == "FAIL")
    skip = sum(1 for r in results if r.status == "SKIP")
    nodes = ", ".join(f"{n}@{p}" for n, p in sorted(attached.items())) or "none attached"
    txt = f"{ok} ok, {fail} failed, {skip} skipped of {len(results)} steps; nodes: {nodes}"
    if absent:
        txt += f"; not attached: {', '.join(absent)}"
    return txt


# ---------------------------------------------------------------- runner

def list_comports() -> Optional[List[Any]]:
    """pyserial's port list, or None when pyserial is not installed."""
    try:
        from serial.tools import list_ports  # type: ignore
    except ImportError:
        return None
    return list(list_ports.comports())


IP_RE = re.compile(r"\bip (\d+\.\d+\.\d+\.\d+)")


def probe_hosts(attached: Dict[str, str], fleet: dict,
                runner: Callable[..., Any] = subprocess.run,
                echo: Callable[[str], None] = print) -> Dict[str, str]:
    """Ask each attached node for its live IP through identity_guard.py and
    override fleet.json's host with it. DHCP hands the bench nodes new
    addresses after every reboot (run 2 on 2026-10-03: the T-Deck came back
    on the T-Beam's old address), so a static host column is wrong within a
    session. Returns the live hosts; nodes without an IP keep their entry."""
    live: Dict[str, str] = {}
    failed: Dict[str, str] = {}
    for name in sorted(attached):
        argv = [sys.executable, str(BENCH / "identity_guard.py"), "--node", name,
                "--port", attached[name]]
        try:
            proc = runner(argv, cwd=ROOT, capture_output=True, text=True, timeout=90)
            out = (proc.stdout or "") + (proc.stderr or "")
            rc = proc.returncode
        except (OSError, subprocess.TimeoutExpired) as exc:
            out, rc = str(exc), 1
        if rc != 0:
            failed[name] = out.strip().splitlines()[-1][:200] if out.strip() else f"identity guard rc {rc}"
            echo(f"identity {name}: FAIL -- {failed[name]}")
            continue
        echo(f"identity {name}: ok")
        m = IP_RE.search(out)
        if m and m.group(1) != "0.0.0.0":
            live[name] = m.group(1)
            if fleet["nodes"][name].get("host") != m.group(1):
                echo(f"host {name}: {fleet['nodes'][name].get('host')} -> {m.group(1)} (live)")
            fleet["nodes"][name]["host"] = m.group(1)
        else:
            echo(f"host {name}: no IP in --info, keeping {fleet['nodes'][name].get('host')}")
    return live, failed


def persist_hosts(fleet: dict, path: Path = FLEET_FILE) -> bool:
    """Write the live hosts back into fleet.json so tools that read the
    registry themselves (ota_regression.py's guard over the 2323 console)
    see the same addresses. Only the host fields change."""
    try:
        on_disk = json.loads(path.read_text())
    except (OSError, ValueError):
        return False
    changed = False
    for name, node in fleet.get("nodes", {}).items():
        if name in on_disk.get("nodes", {}) and on_disk["nodes"][name].get("host") != node.get("host"):
            on_disk["nodes"][name]["host"] = node.get("host")
            changed = True
    if changed:
        path.write_text(json.dumps(on_disk, indent=2) + "\n")
    return changed


def port_busy(dev: str) -> bool:
    try:
        r = subprocess.run(["lsof", dev], capture_output=True, text=True, timeout=10)
    except (OSError, subprocess.TimeoutExpired):
        return False
    return r.returncode == 0 and bool(r.stdout.strip())


def run_steps(steps: List[Step], out: Path, runner: Callable[..., Any] = subprocess.run,
              echo: Callable[[str], None] = print) -> List[Result]:
    results: List[Result] = []
    dead_nodes: Dict[str, str] = {}
    for i, s in enumerate(steps, 1):
        safe = s.name.replace(" ", "-").replace("/", "_")
        log = out / f"stage3-{i:02d}-{safe}.log"
        if s.node in dead_nodes:
            results.append(Result(s.name, s.node, s.argv, "SKIP", detail=dead_nodes[s.node]))
            echo(f"SKIP   {s.name}: {dead_nodes[s.node]}")
            continue
        t0 = time.monotonic()
        with open(log, "w") as fh:
            fh.write("$ " + " ".join(s.argv) + "\n")
            fh.flush()
            try:
                extra = {"env": {**os.environ, **s.env}} if s.env else {}
                proc = runner(s.argv, cwd=ROOT, stdout=fh, stderr=subprocess.STDOUT,
                              timeout=s.timeout_s, **extra)
                rc = proc.returncode
                detail = ""
            except subprocess.TimeoutExpired:
                rc = -1
                detail = f"timeout after {s.timeout_s} s"
        secs = time.monotonic() - t0
        if rc == 3:
            status, detail = "SKIP", "tool reported not applicable (exit 3), see log"
        else:
            status = "OK" if rc == 0 else "FAIL"
        results.append(Result(s.name, s.node, s.argv, status, rc, round(secs, 1), str(log), detail))
        echo(f"{status:6s} {s.name} ({secs:.0f} s){' -- ' + detail if detail else ''}")
        if status == "FAIL" and s.gate:
            dead_nodes[s.node] = f"gate step '{s.name}' failed"
    return results


def main(argv: Optional[List[str]] = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--out", default=None, help="log/summary directory (default tools/bench/runs/bench-<ts>)")
    ap.add_argument("--dry-run", action="store_true", help="print the plan, run nothing")
    ap.add_argument("--flash", action="store_true", help="build and flash every attached node first")
    ap.add_argument("--instrument", dest="instrument", action="store_true", default=True,
                    help="build INSTRUMENT_ENABLED=1 images for --flash and the OTA step (default: on; the "
                         "harnesses need them and the fleet runs them; wipes .pio/build)")
    ap.add_argument("--no-instrument", dest="instrument", action="store_false",
                    help="build release images instead (the harness steps will answer 'wrong command')")
    ap.add_argument("--no-ota", action="store_true", help="skip the OTA regression on the WiFi boards")
    ap.add_argument("--extudp", action="store_true", help="also run the RAK EXTUDP scenario (TM-43, long)")
    ap.add_argument("--deepsleep", action="store_true", help="also run DS-03 on a Heltec V3")
    ap.add_argument("--no-badge", action="store_true", help="skip the web GUI badge test (REG-04) on the WiFi boards")
    ap.add_argument("--no-mesh", action="store_true", help="skip the cross-node mesh exchange (REG-05)")
    ap.add_argument("--no-prepare", action="store_true",
                    help="skip prepare_node.py (groups, debug, GPS, track) before each harness")
    ap.add_argument("--soak-seconds", type=int, default=600, help="extudp soak tail (default 600)")
    ap.add_argument("--fleet", type=Path, default=FLEET_FILE)
    a = ap.parse_args(argv)

    fleet = load_fleet(a.fleet)
    ports = list_comports()
    if ports is None:
        print("pyserial missing: pip install pyserial (or: uv run --with pyserial ...)")
        return 2
    attached = discover_ports(fleet, ports)
    absent = sorted(set(fleet.get("nodes", {})) - set(attached))
    out = Path(a.out) if a.out else ROOT / BENCH / "runs" / f"bench-{time.strftime('%Y%m%d-%H%M%S')}"

    busy = {n: d for n, d in attached.items() if not a.dry_run and port_busy(d)}
    for n, d in busy.items():
        print(f"SKIP   {n}: {d} is held by another process (lsof) -- close it first")
        attached.pop(n)

    probed = False
    if attached and not a.dry_run:
        _live, failed = probe_hosts(attached, fleet)
        for n, why in failed.items():
            print(f"SKIP   {n}: identity guard failed -- {why}")
            attached.pop(n)
        if persist_hosts(fleet, a.fleet):
            print(f"fleet.json hosts updated: {a.fleet}")
        probed = True
    steps = plan(fleet, attached, flash=a.flash, instrument=a.instrument, identity_steps=not probed, ota=not a.no_ota, extudp=a.extudp,
                 deepsleep=a.deepsleep, soak_seconds=a.soak_seconds,
                 badge=not a.no_badge, mesh=not a.no_mesh, prepare=not a.no_prepare, out=out)
    print(f"attached: {', '.join(f'{n}@{d}' for n, d in sorted(attached.items())) or 'none'}")
    if absent:
        print(f"not attached: {', '.join(absent)}")
    if a.dry_run:
        for s in steps:
            print(f"PLAN   {s.name:32s} {' '.join(s.argv)}")
        if not steps:
            print("PLAN   nothing to run -- no fleet node on USB")
        return 0
    if not attached:
        out.mkdir(parents=True, exist_ok=True)
        (out / "bench-summary.json").write_text(json.dumps(
            {"summary": summarize([], attached, absent), "steps": []}, indent=2))
        print("no fleet node attached over USB -- nothing to run")
        return 1

    out.mkdir(parents=True, exist_ok=True)
    results = run_steps(steps, out)
    text = summarize(results, attached, absent)
    (out / "bench-summary.json").write_text(json.dumps(
        {"summary": text, "steps": [asdict(r) for r in results]}, indent=2))
    print(text)
    return 1 if any(r.status == "FAIL" for r in results) else 0


if __name__ == "__main__":
    sys.exit(main())
