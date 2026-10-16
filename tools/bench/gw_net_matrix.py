#!/usr/bin/env python3
"""Gateway server selection matrix on a real node (RAK4631 DK5EN-90 or Heltec V3 DK5EN-1).

    python3 tools/bench/gw_net_matrix.py --node rak --ip 192.168.68.62 --dry-run
    python3 tools/bench/gw_net_matrix.py --node heltec --ip 192.168.68.61

For every network (N1 DHCP, N2/N3 static with two different gateways), every
`--setnet` mode and every server code: set the node up, reboot once, collect the
unconditional `[GW];srv|keep|rx` and `[NTP];ok` lines and judge them against the
oracle (tools/gw_servers_dump.cpp, built from the firmware's gw_servers.cpp).

Only a fixed whitelist of console commands is ever sent (see `check_cmd`); never
message text, never a TX power change. Serial plumbing is rak_harness.RakSession; the
RAK re-enumerates USB on every reboot, so the port is re-resolved by USB serial
number each time it vanishes.
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Optional

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))

NODES: dict[str, dict[str, str]] = {
    "rak": {"call": "DK5EN-90", "serial": "230D6EBB3266D20E"},
    "heltec": {"call": "DK5EN-1", "serial": "0001"},
}
MODE_NUM = {"auto": 2, "inet": 3, "hamnet": 1}
MODE_LABEL = {"auto": "auto", "inet": "Internet", "hamnet": "Hamnet"}
GW_N2 = "192.168.68.1"
GW_N3 = "192.168.68.64"
MASK = "255.255.252.0"
MAX_TXPOWER = 2

# ---------------------------------------------------------------- whitelist

_IP = r"(?:\d{1,3}\.){3}\d{1,3}|0"
WHITELIST = [
    re.compile(p)
    for p in (
        r"--info",
        r"--debug on",
        r"--gateway on",
        r"--gateway srv [A-Z]{2}",
        r"--setnet(?: (?:auto|inet|hamnet))?",
        rf"--setown(?:ip|gw|ms|dns) (?:{_IP})",
        r"--reboot",
    )
]


def check_cmd(cmd: str) -> str:
    """Return cmd if whitelisted, else raise ValueError (never message text)."""
    if cmd.startswith(":") or not any(p.fullmatch(cmd) for p in WHITELIST):
        raise ValueError(f"command not whitelisted: {cmd!r}")
    return cmd


# ---------------------------------------------------------------- parsing

RE_SRV = re.compile(r"\[GW\];srv;(\w+);host;([^;\s]+);path;(\w+);ms;(\d+)")
RE_KEEP = re.compile(r"\[GW\];keep;tx;ok;1;ms;(\d+)")
RE_RX = re.compile(r"\[GW\];rx;type;(BEAT|CONF)")
RE_NTP = re.compile(r"\[NTP\];ok;epoch;(\d+);rtt;(\d+)")
RE_NTP_BAD = re.compile(r"\[NTP\];(timeout|txfail|kod)")
RE_BOOT = re.compile(r"\[BOOT\];ready")
RE_NETMODE = re.compile(r"Network mode: (\w+) \(in use: (\w+)\)")
RE_CALL = re.compile(r"\.\.\.Call: <([^>]*)>")
RE_PWR = re.compile(r"TXPWR (-?\d+) dBm")
RE_IPINFO = re.compile(r"\.\.\.IP address\s*:\s*(\d+\.\d+\.\d+\.\d+)")


def parse_lines(lines: list[str]) -> dict[str, Any]:
    """Collect the evidence of one boot from received lines (last srv line wins)."""
    o: dict[str, Any] = {
        "srv": None,
        "host": None,
        "path": None,
        "keep": False,
        "answer": False,
        "ntp": False,
        "ntp_fail": None,
        "netmode": None,
        "in_use": None,
    }
    for ln in lines:
        if m := RE_SRV.search(ln):
            o["srv"], o["host"], o["path"] = m.group(1), m.group(2), m.group(3)
        if RE_KEEP.search(ln):
            o["keep"] = True
        if RE_RX.search(ln):
            o["answer"] = True
        if RE_NTP.search(ln):
            o["ntp"] = True
        if m := RE_NTP_BAD.search(ln):
            o["ntp_fail"] = m.group(1)
        if m := RE_NETMODE.search(ln):
            o["netmode"], o["in_use"] = m.group(1), m.group(2)
    return o


def parse_info(lines: list[str]) -> dict[str, Any]:
    info: dict[str, Any] = {"call": None, "txpower": None, "ip": None}
    for ln in lines:
        if m := RE_CALL.search(ln):
            info["call"] = m.group(1)
        if m := RE_PWR.search(ln):
            info["txpower"] = int(m.group(1))
        if m := RE_IPINFO.search(ln):
            info["ip"] = m.group(1)
    return info


def complete(o: dict[str, Any]) -> bool:
    return bool(o["srv"] and o["keep"] and o["answer"] and o["ntp"])


# ---------------------------------------------------------------- oracle


def build_oracle(src: Path, out_dir: Path) -> Optional[Path]:
    cxx = shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
    if not cxx:
        return None
    exe = out_dir / "gw_servers_dump"
    r = subprocess.run(
        [cxx, "-std=gnu++17", "-I", str(src), str(src / "gw_servers.cpp"),
         str(ROOT / "tools" / "gw_servers_dump.cpp"), "-o", str(exe)],
        capture_output=True, text=True, check=False,
    )  # fmt: skip
    if r.returncode != 0:
        raise RuntimeError(f"oracle build failed:\n{r.stderr}")
    return exe


def platform_for(node: str, static: bool) -> str:
    if node == "heltec":
        return "esp32"
    return "nrf_static" if static else "nrf_dyn"


def run_oracle(exe: Path, rows: list[tuple[str, str, str, int]]) -> list[dict[str, Any]]:
    """rows: (platform, code, own_ip, hamnet_only) -> oracle JSON objects."""
    stdin = "".join(f"{p} {c} {ip} {m} -\n" for p, c, ip, m in rows)
    r = subprocess.run([str(exe)], input=stdin, capture_output=True, text=True, check=False)
    return [json.loads(x) for x in r.stdout.splitlines() if x.strip()]


# ---------------------------------------------------------------- judging


def selected_net(mode: str, own_ip: str) -> str:
    """Network the firmware reports as "in use" for a mode (gwSelectNet)."""
    if mode in ("hamnet", "inet"):
        return mode
    return "hamnet" if own_ip.startswith("44.") else "inet"


def judge(code: str, mode: str, exp: dict[str, Any], obs: dict[str, Any], node: str) -> list[str]:
    """Return the list of failing checks (empty list = PASS)."""
    fails: list[str] = []
    if obs["srv"] != code:
        fails.append(f"srv code {obs['srv']!r} != {code!r}")
    if exp.get("error"):
        fails.append("oracle has no entry")
    else:
        if obs["path"] != exp["path"]:
            fails.append(f"path {obs['path']!r} != {exp['path']!r}")
        host, want = obs["host"], exp["srv"]
        if host != want:
            is_ip = bool(host and re.fullmatch(r"\d+\.\d+\.\d+\.\d+", host))
            if not (node == "heltec" and is_ip and not re.fullmatch(r"[\d.]+", want)):
                fails.append(f"host {host!r} != {want!r}")
    if not obs["keep"]:
        fails.append("no [GW];keep;tx;ok")
    if not obs["answer"]:
        fails.append("no server answer (BEAT/CONF)")
    if not obs["ntp"]:
        fails.append("no [NTP];ok" + (f" ({obs['ntp_fail']})" if obs["ntp_fail"] else ""))
    # bare --setnet prints the mode as "auto" / "Internet" / "Hamnet" (command_functions.cpp)
    want_mode = MODE_LABEL[mode]
    if obs["netmode"] != want_mode:
        fails.append(f"netmode {obs['netmode']!r} != {want_mode!r}")
    # "in use" is the SELECTED network (gwNetInUse: gwSelectNet of mode + own address), not the path
    # of the server row: IT with --setnet hamnet says "hamnet" while [GW];srv says path inet (intended,
    # operator 2026-10-16). auto follows the own address (44.x -> hamnet).
    want_net = selected_net(mode, exp.get("own_ip", ""))
    if obs["in_use"] != want_net:
        fails.append(f"in-use {obs['in_use']!r} != {want_net!r}")
    return fails


# ---------------------------------------------------------------- cases


@dataclass
class Net:
    name: str
    static: bool
    gw: str = ""


@dataclass
class Case:
    net: Net
    mode: str
    server: str
    expected: dict[str, Any] = field(default_factory=dict)


def make_nets(names: list[str]) -> list[Net]:
    table = {"N1": Net("N1", False), "N2": Net("N2", True, GW_N2), "N3": Net("N3", True, GW_N3)}
    return [table[n] for n in names]


def make_cases(
    node: str, nets: list[Net], modes: list[str], servers: list[str], ips: dict[str, str],
    exe: Optional[Path],
) -> list[Case]:  # fmt: skip
    cases = [Case(n, m, s) for n in nets for m in modes for s in servers]
    if exe is None:
        return cases
    rows = [
        (platform_for(node, c.net.static), c.server, ips[c.net.name], MODE_NUM[c.mode])
        for c in cases
    ]
    for c, e in zip(cases, run_oracle(exe, rows)):
        c.expected = e
    return cases


# ---------------------------------------------------------------- serial


class Node:
    """RakSession plus reopen-by-USB-serial when the port vanishes."""

    def __init__(self, name: str, log_path: Path) -> None:
        import rak_harness as rh

        self.name = name
        self.serial_no = NODES[name]["serial"]
        self.rh = rh
        self.port = self.resolve(30.0)
        self.s = rh.RakSession(self.port, log_path=log_path)
        self.s.open()
        self.settle = rh.settle_for(self.port)

    def resolve(self, wait: float) -> str:
        from serial.tools import list_ports

        end = time.monotonic() + wait
        while True:
            for p in list_ports.comports():
                if p.serial_number == self.serial_no:
                    return p.device
            if time.monotonic() >= end:
                raise TimeoutError(f"no USB device with serial {self.serial_no}")
            time.sleep(0.5)

    def reopen(self) -> None:
        self.s.close()
        self.s.port = self.resolve(40.0)
        self.s.dtr = self.rh.dtr_for(self.s.port)
        self.s._partial = ""  # noqa: SLF001
        self.s.open()

    def pump(self, seconds: float) -> None:
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            if self.s.ser is None:
                self.reopen()
            if not self.s.pump(min(0.5, max(0.05, end - time.monotonic()))):
                time.sleep(0.5)  # port dropped; next loop reopens

    def send(self, cmd: str) -> int:
        check_cmd(cmd)
        if self.s.ser is None:
            self.reopen()
        return self.s.send(cmd)

    def mark(self) -> int:
        return len(self.s.lines)

    def since(self, idx: int) -> list[str]:
        return self.s.lines_since(idx)

    def wait(self, pattern: str, timeout: float, since: int) -> bool:
        rx = re.compile(pattern)
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if any(rx.search(ln) for ln in self.since(since)):
                return True
            self.pump(0.3)
        return any(rx.search(ln) for ln in self.since(since))

    def reboot_wait(self, timeout: float = 60.0) -> int:
        """--reboot, return index of the last boot-marker line (or reboot mark)."""
        idx = self.mark()
        self.send("--reboot")
        self.wait(RE_BOOT.pattern, timeout, idx)
        self.pump(2.0)
        return idx


def last_boot_start(lines: list[str]) -> int:
    """Index of the last [GW];srv line: the firmware prints it before [BOOT];ready, once per
    (re)connect, so the evidence of the latest boot starts there (an extra auto reboot is fine)."""
    pos = 0
    for i, ln in enumerate(lines):
        if RE_SRV.search(ln):
            pos = i
    return pos


def run_case(nd: Node, c: Case, timeout: float) -> dict[str, Any]:
    t0 = time.monotonic()
    idx = nd.mark()
    nd.send(f"--gateway srv {c.server}")
    nd.send(f"--setnet {c.mode}")
    nd.send("--reboot")
    seen_boot = nd.wait(RE_BOOT.pattern + "|" + RE_SRV.pattern, 60.0, idx)
    end = t0 + timeout
    obs = parse_lines([])
    while time.monotonic() < end:
        lines = nd.since(idx)
        obs = parse_lines(lines[last_boot_start(lines) :])
        if complete(obs):
            break
        nd.pump(0.5)
    nd.pump(1.0)
    q = nd.mark()
    nd.send("--setnet")
    nd.wait(RE_NETMODE.pattern, 10.0, q)
    nm = parse_lines(nd.since(q))
    lines = nd.since(idx)
    obs = parse_lines(lines[last_boot_start(lines) :])
    obs["netmode"], obs["in_use"] = nm["netmode"], nm["in_use"]
    fails = judge(c.server, c.mode, c.expected, obs, nd.name)
    if not seen_boot:
        fails.append("no boot marker or srv line after reboot")
    return {
        "net": c.net.name, "mode": c.mode, "server": c.server,
        "expected": {k: c.expected.get(k) for k in ("srv", "path", "error")},
        "observed": obs, "checks": fails,
        "verdict": "FAIL" if fails else "PASS",
        "seconds": round(time.monotonic() - t0, 1),
    }  # fmt: skip


def switch_net(nd: Node, net: Net, ip: str, dns: str) -> Optional[str]:
    """Apply the network, reboot, return the node's IP as reported by --info."""
    if net.static:
        vals = {"ip": ip, "gw": net.gw, "ms": MASK, "dns": dns}
    else:
        vals = {"ip": "0", "gw": "0", "ms": "0", "dns": "0"}
    for k in ("ip", "gw", "ms", "dns"):
        nd.send(f"--setown{k} {vals[k]}")
        nd.pump(0.5)
    nd.reboot_wait(90.0)
    nd.pump(15.0)  # tolerate the +15 s auto reboot of the own* settings
    q = nd.mark()
    nd.send("--info")
    nd.pump(4.0)
    return parse_info(nd.since(q))["ip"]


# ---------------------------------------------------------------- output


def summary(results: list[dict[str, Any]]) -> str:
    out = [f"{'net':<4} {'mode':<7} {'srv':<4} {'verdict':<7} {'s':>6}  failing checks"]
    for r in results:
        out.append(
            f"{r['net']:<4} {r['mode']:<7} {r['server']:<4} {r['verdict']:<7} "
            f"{r['seconds']:>6}  {'; '.join(r['checks'])}"
        )
    n_pass = sum(r["verdict"] == "PASS" for r in results)
    out.append(f"{n_pass}/{len(results)} PASS")
    return "\n".join(out)


def print_plan(cases: list[Case]) -> None:
    for c in cases:
        e = c.expected
        if e.get("error") or not e:
            exp = "no oracle entry" if e else "oracle unavailable (no compiler)"
        else:
            exp = f"srv={c.server} host={e['srv']} path={e['path']} in-use={selected_net(c.mode, e.get('own_ip', ''))}"
        print(f"{c.net.name} {c.mode:<6} {c.server}: {exp}")


def parse_args(argv: Optional[list[str]] = None) -> argparse.Namespace:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--node", choices=sorted(NODES), required=True)
    ap.add_argument("--nets", default="N1,N2,N3")
    ap.add_argument("--modes", default="auto,inet,hamnet")
    ap.add_argument("--servers", default="OE,DL,IT,HC,HB")
    ap.add_argument("--ip", default="", help="static IP for N2/N3 (also the oracle IP in --dry-run)")
    ap.add_argument("--dns", default="217.237.150.188")
    ap.add_argument("--case-timeout", type=float, default=100.0)
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--src", default=str(ROOT / "src"))
    ap.add_argument("--out-dir", default=str(ROOT / "tools" / "bench" / "runs" / "harness"))
    return ap.parse_args(argv)


def main(argv: Optional[list[str]] = None) -> int:
    a = parse_args(argv)
    nets = make_nets(a.nets.split(","))
    modes, servers = a.modes.split(","), a.servers.split(",")
    if any(n.static for n in nets) and not a.ip:
        print("--ip is required for static networks", file=sys.stderr)
        return 2
    scratch = Path(tempfile.mkdtemp(prefix="gwnet_oracle_"))
    exe = build_oracle(Path(a.src), scratch)
    ips = {n.name: a.ip or "192.168.68.62" for n in nets}
    if a.dry_run:
        print_plan(make_cases(a.node, nets, modes, servers, ips, exe))
        return 0

    ts = time.strftime("%Y%m%d-%H%M%S")
    out = Path(a.out_dir)
    out.mkdir(parents=True, exist_ok=True)
    nd = Node(a.node, out / f"gwnet_{a.node}_{ts}.log")
    jsonl = (out / f"gwnet_{a.node}_{ts}.jsonl").open("w", encoding="utf-8")
    results: list[dict[str, Any]] = []
    signal.signal(signal.SIGINT, signal.default_int_handler)
    try:
        nd.pump(nd.settle)
        nd.wait(RE_BOOT.pattern, 30.0, 0)
        q = nd.mark()
        nd.send("--info")
        nd.pump(4.0)
        info = parse_info(nd.since(q))
        want = NODES[a.node]["call"]
        if info["call"] != want or info["txpower"] is None or info["txpower"] > MAX_TXPOWER:
            print(f"refusing: call={info['call']!r} (want {want}) txpower={info['txpower']}",
                  file=sys.stderr)  # fmt: skip
            return 2
        nd.send("--debug on")
        nd.send("--gateway on")
        nd.pump(1.0)
        for net in nets:
            ip = switch_net(nd, net, a.ip, a.dns) or ips[net.name]
            ips[net.name] = ip
            for c in make_cases(a.node, [net], modes, servers, ips, exe):
                r = run_case(nd, c, a.case_timeout)
                results.append(r)
                jsonl.write(json.dumps(r) + "\n")
                jsonl.flush()
                print(f"{r['net']} {r['mode']} {r['server']}: {r['verdict']} {r['checks']}",
                      flush=True)  # fmt: skip
    except KeyboardInterrupt:
        print("interrupted", file=sys.stderr)
    finally:
        jsonl.close()
        nd.s.close()
        print(summary(results))
    return 0 if all(r["verdict"] == "PASS" for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
