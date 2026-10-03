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

What is deliberately NOT here: the cross-node LoRa exchange (TM-26, no tool
yet), the AP-reboot run (TM-38, the bench Mac loses its own WLAN), the CDC
unplug proof (needs a hand on the cable) and the operator eye/ear checks --
see docs/automation-runner-runbook.md 2.4/2.5/2.7.
"""
from __future__ import annotations

import argparse
import json
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


@dataclass
class Step:
    name: str
    node: str
    argv: List[str]
    timeout_s: int = HARNESS_TIMEOUT_S
    gate: bool = False          # a failing gate step skips the node's later steps


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


def flash_steps(name: str, env: str, dev: str) -> List[Step]:
    build = Step(f"build {env}", name, ["pio", "run", "-e", env], FLASH_TIMEOUT_S, gate=True)
    if env in TDECK_ENVS or env in RAK_ENVS:
        # Native USB (T-Deck) and serial DFU (RAK, must be running, not in UF2
        # mode) both go through pio's own uploader; the explicit --upload-port
        # keeps pio's autodetect off the wrong board.
        up = Step(f"flash {env}", name,
                  ["pio", "run", "-e", env, "--target", "upload", "--upload-port", dev],
                  FLASH_TIMEOUT_S, gate=True)
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
    return [build, up]


def plan(fleet: dict, attached: Dict[str, str], *, flash: bool = False, ota: bool = True,
         extudp: bool = False, deepsleep: bool = False, soak_seconds: int = 600,
         out: Path = Path(".")) -> List[Step]:
    """The ordered step list for the attached nodes. Pure: no I/O."""
    steps: List[Step] = []
    nodes = fleet.get("nodes", {})
    esp32_ports = [attached[n] for n in sorted(attached) if nodes[n].get("family") == "esp32"]
    py = sys.executable
    for name in sorted(attached):
        node = nodes[name]
        env = node["board"]
        dev = attached[name]
        steps.append(Step(f"identity {name}", name,
                          [py, str(BENCH / "identity_guard.py"), "--node", name, "--port", dev],
                          timeout_s=60, gate=True))
        if flash:
            steps.extend(flash_steps(name, env, dev))
        summary = str(out / f"{name}-harness.json")
        if env in TDECK_ENVS:
            steps.append(Step(f"tdeck harness {name}", name,
                              [py, str(BENCH / "tdeck_harness.py"), "--scenario", "all",
                               "--port", dev, "--out", summary]))
        elif env in RAK_ENVS:
            argv = [py, str(BENCH / "rak_harness.py"), "--scenario", "all",
                    "--port", dev, "--out", summary]
            if esp32_ports:
                argv += ["--peer-port", esp32_ports[0]]
            steps.append(Step(f"rak harness {name}", name, argv))
            if extudp:
                steps.append(Step(f"rak extudp {name}", name,
                                  [py, str(BENCH / "rak_harness.py"), "--scenario", "extudp",
                                   "--port", dev, "--out", str(out / f"{name}-extudp.json"),
                                   "--soak-seconds", str(soak_seconds)],
                                  timeout_s=soak_seconds + 10 * 60))
        else:
            steps.append(Step(f"oled harness {name}", name,
                              [py, str(BENCH / "oled_harness.py"), "--scenario", "all",
                               "--port", dev, "--out", summary]))
        if ota and node.get("family") == "esp32" and node.get("host"):
            steps.append(Step(f"ota regression {name}", name,
                              [py, str(BENCH / "ota_regression.py"), "--port", dev, "--env", env,
                               "--ip", node["host"], "--settings-check"], OTA_TIMEOUT_S))
        if deepsleep and env in DEEPSLEEP_ENVS:
            steps.append(Step(f"deepsleep {name}", name,
                              [py, str(BENCH / "deepsleep_button.py"), "--port", dev]))
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
                proc = runner(s.argv, cwd=ROOT, stdout=fh, stderr=subprocess.STDOUT,
                              timeout=s.timeout_s)
                rc = proc.returncode
                detail = ""
            except subprocess.TimeoutExpired:
                rc = -1
                detail = f"timeout after {s.timeout_s} s"
        secs = time.monotonic() - t0
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
    ap.add_argument("--no-ota", action="store_true", help="skip the OTA regression on the WiFi boards")
    ap.add_argument("--extudp", action="store_true", help="also run the RAK EXTUDP scenario (TM-43, long)")
    ap.add_argument("--deepsleep", action="store_true", help="also run DS-03 on a Heltec V3")
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

    steps = plan(fleet, attached, flash=a.flash, ota=not a.no_ota, extudp=a.extudp,
                 deepsleep=a.deepsleep, soak_seconds=a.soak_seconds, out=out)
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
