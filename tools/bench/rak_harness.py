#!/usr/bin/env python3
"""RAK4631 (nRF52840, W5100S gateway) bench harness -- the fourth platform of the
regression goal (BACKLOG TM-25/TM-26), sister of tdeck_harness.py / oled_harness.py.

    python3 tools/bench/rak_harness.py --list
    python3 tools/bench/rak_harness.py --scenario all
    python3 tools/bench/rak_harness.py --scenario boot,instr --port /dev/cu.usbmodem201301

What is different on the RAK (memory rak4631-serial-testing-pitfalls):
  * opening the port does NOT reset the node; it needs dtr=True or it stays silent;
  * `--reboot` re-enumerates USB (the port vanishes and comes back), so the boot
    scenario sends --reboot, waits for the device node to disappear and return,
    reopens and timestamps every boot phase on the host clock;
  * the firmware prints [BOOT];ready;ms;N;ip;X;eth;Y once per boot (TM-25);
  * a node in gateway mode without an Ethernet link can freeze the loop (N-20);
    the instr scenario's loop max and the [INSTR-LOOP];gap lines make that visible.
Run from tools/bench/runs/ so the raw logs land there.
"""
import argparse
import json
import os
import re
import sys
import time
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence, Tuple

try:
    import serial  # pyserial
except ImportError:  # pragma: no cover
    serial = None  # type: ignore[assignment]

DEFAULT_PORT = "/dev/cu.usbmodem201301"   # DK5EN-90
BAUD = 115200
CRASH = r"HardFault|assert|Backtrace|\[BOOT\] RESETREAS=0x0000000[28]"   # 0x2 watchdog, 0x8 lockup
SEP = r"[; ]"
PHASES = [
    ("start_client", r"\[INIT\] START CLIENT"),
    ("flash", r"\[INIT\]\.\.\.FLASH"),
    ("client_started", r"\[INIT\]\.\.\.CLIENT STARTED"),
    ("eth_init", r"Initialize Ethernet"),
    ("eth_ip", r"Ethernet\.localIP\(\)"),
    ("gateway", r"GATEWAY 4\.0 RUNNING"),
    ("ready", r"\[BOOT\]" + SEP + "ready"),
]


class RakSession:
    """Held-open serial session with a host-timestamped line log."""

    def __init__(self, port: str, log_path: Optional[Path] = None) -> None:
        self.port = port
        self.ser: Optional[Any] = None
        self.lines: List[Tuple[float, str]] = []      # (host monotonic, line)
        self._partial = ""
        self.log_path = log_path or Path(f"rak_run_{time.strftime('%Y%m%d-%H%M%S')}.log")
        self._logf = open(self.log_path, "a", buffering=1, encoding="utf-8")

    def open(self, wait_for_node: float = 20.0) -> None:
        if serial is None:
            raise RuntimeError("pyserial is required")
        deadline = time.monotonic() + wait_for_node
        last_err: Optional[Exception] = None
        while time.monotonic() < deadline:
            if os.path.exists(self.port):
                try:
                    s = serial.Serial()
                    s.port = self.port
                    s.baudrate = BAUD
                    s.timeout = 0.2
                    s.dtr = True      # the RAK stays silent without DTR
                    s.rts = False
                    s.open()
                    self.ser = s
                    return
                except Exception as e:  # noqa: BLE001
                    last_err = e
            time.sleep(0.2)
        raise TimeoutError(f"could not open {self.port}: {last_err}")

    def close(self) -> None:
        if self.ser is not None:
            try:
                self.ser.close()
            except Exception:  # noqa: BLE001
                pass
            self.ser = None

    def pump(self, seconds: float) -> bool:
        """Read for `seconds`; returns False if the port dropped (USB re-enumeration)."""
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            try:
                chunk = self.ser.read(4096) if self.ser else b""
            except Exception:  # noqa: BLE001
                self.close()
                return False
            if chunk:
                text = self._partial + chunk.decode("utf-8", errors="replace")
                parts = text.split("\n")
                self._partial = parts.pop()
                now = time.monotonic()
                for l in parts:
                    l = l.rstrip("\r")
                    self.lines.append((now, l))
                    self._logf.write(f"{time.strftime('%H:%M:%S')} {l}\n")
        return True

    def send(self, cmd: str) -> int:
        idx = len(self.lines)
        self._logf.write(f"{time.strftime('%H:%M:%S')} >> {cmd}\n")
        if self.ser is not None:
            self.ser.write((cmd + "\n").encode())
            self.ser.flush()
        return idx

    def wait_for(self, pattern: str, timeout: float, since: int = 0) -> Optional[re.Match]:
        end = time.monotonic() + timeout
        rx = re.compile(pattern)
        while True:
            for _, l in self.lines[since:]:
                m = rx.search(l)
                if m:
                    return m
            if time.monotonic() >= end:
                return None
            since = len(self.lines)
            if not self.pump(0.2):
                return None

    def lines_since(self, idx: int) -> List[str]:
        return [l for _, l in self.lines[idx:]]


def _field(line: Optional[str], key: str) -> Optional[int]:
    m = re.search(re.escape(key) + SEP + r"(-?\d+)", line or "")
    return int(m.group(1)) if m else None


def scenario_boot(s: RakSession, args: argparse.Namespace) -> Dict[str, Any]:
    """--reboot, then every boot phase on the host clock (t=0 at the command),
    the firmware's own [BOOT];ready ms, crash markers and -- because one --reboot
    was seen to produce two boots on 2026-08-29 -- the number of boots."""
    s.send("--reboot")
    t0 = time.monotonic()
    s.close()
    gone = False
    for _ in range(60):
        time.sleep(0.1)
        if not os.path.exists(s.port):
            gone = True
            break
    t_gone = time.monotonic() - t0 if gone else None
    boots: List[Dict[str, Any]] = []
    cur: Dict[str, Any] = {}
    end = time.monotonic() + args.boot_seconds
    idx = 0
    while time.monotonic() < end:
        if s.ser is None:
            try:
                s.open(wait_for_node=max(0.5, end - time.monotonic()))
            except TimeoutError:
                break
            idx = len(s.lines)
        if not s.pump(0.5):
            continue
        for t, l in s.lines[idx:]:
            for name, pat in PHASES:
                if re.search(pat, l):
                    if name == "start_client" and cur:
                        boots.append(cur)
                        cur = {}
                    cur.setdefault(name, round(t - t0, 2))
                    if name == "ready":
                        cur["ready_fw_ms"] = _field(l, "ms")
                        cur["ip"] = _field(l, "ip")
                        cur["eth"] = _field(l, "eth")
            if re.search(r"RESETREAS=0x", l):
                cur.setdefault("resetreas", l.split("RESETREAS=")[-1].strip())
        idx = len(s.lines)
    if cur:
        boots.append(cur)
    crash = [l for _, l in s.lines if re.search(CRASH, l)]
    ok = (
        s.ser is not None
        and len(boots) >= 1
        and "ready" in boots[-1]
        and not crash
    )
    return {"ok": ok, "port_gone_after_s": t_gone, "boots": boots, "boot_count": len(boots),
            "crash_lines": crash[:5], "double_boot": len(boots) > 1}


def scenario_info(s: RakSession, args: argparse.Namespace) -> Dict[str, Any]:
    """--info answers; call sign, hardware and the gateway/Ethernet state."""
    idx = s.send("--info")
    m = s.wait_for(r"--MeshCom|Firmware|CALL", 4.0, since=idx)
    s.pump(1.5)
    lines = s.lines_since(idx)
    call = next((l for l in lines if re.search(r"\bCALL\b|Call:", l)), None)
    hw = next((l for l in lines if "NODE" in l and "<" in l), None)
    gw = next((l for l in lines if re.search(r"GATEWAY|Gateway", l)), None)
    return {"ok": m is not None and len(lines) > 5, "answered_lines": len(lines),
            "call_line": call, "hw_line": hw, "gateway_line": gw}


def scenario_instr(s: RakSession, args: argparse.Namespace) -> Dict[str, Any]:
    """Loop period, heap and per-section stall attribution over --instr-seconds
    of undisturbed running: [INSTR-LOOP] max, [INSTR-SECT] per subsystem,
    [INSTR-GAPS] count and every [INSTR-LOOP];gap line with its section."""
    idx0 = s.send("--instreset")
    s.wait_for(r"\[INSTR\]" + SEP + "reset", 3.0, since=idx0)
    s.pump(args.instr_seconds)
    idx = s.send("--instr")
    s.wait_for(r"\[INSTR-GUI\]|\[INSTR-GAPS\]", 4.0, since=idx)
    s.pump(1.0)
    lines = s.lines_since(idx)
    loop = next((l for l in lines if "[INSTR-LOOP]" in l and "gap" not in l), None)
    heap = next((l for l in lines if "[INSTR-HEAP]" in l), None)
    sects = [l for l in lines if "[INSTR-SECT]" in l]
    gaps_line = next((l for l in lines if "[INSTR-GAPS]" in l), None)
    gap_events = [l for l in s.lines_since(idx0) if "[INSTR-LOOP]" in l and "gap" in l]
    sections = {}
    for l in sects:
        m = re.search(r"\[INSTR-SECT\]" + SEP + r"(\w+)", l)
        if m:
            sections[m.group(1)] = {"n": _field(l, "n"), "avg_us": _field(l, "avg_us"), "max_us": _field(l, "max_us")}
    loop_max = _field(loop, "max_us")
    ok = loop is not None and loop_max is not None and loop_max < args.loop_budget_us
    return {"ok": ok, "seconds": args.instr_seconds, "loop_n": _field(loop, "n"), "loop_avg_us": _field(loop, "avg_us"),
            "loop_max_us": loop_max, "gaps": _field(gaps_line, "n"), "gap_events": gap_events[:10],
            "sections": sections, "heap_free": _field(heap, "int_free"), "heap_largest": _field(heap, "int_largest"),
            "worst_section": max(sections.items(), key=lambda kv: kv[1]["max_us"] or 0)[0] if sections else None}


def scenario_lora(s: RakSession, args: argparse.Namespace) -> Dict[str, Any]:
    """--sendpos must produce a LoRa transmission: OnTXDone (printed with
    --debug on) within 15 s; CSMA may hold the frame while the channel is busy."""
    s.send("--debug on")
    s.pump(0.5)
    idx = s.send("--sendpos")
    m = s.wait_for(r"OnTXDone|TX-LoRa", 15.0, since=idx)
    s.pump(1.0)
    tx_lines = [l for l in s.lines_since(idx) if re.search(r"OnTXDone|TX-LoRa", l)]
    s.send("--debug off")
    s.pump(0.5)
    # Cross-node proof when --peer-port is given: the peer (an ESP32 bench node;
    # opening its port reboots it) must list DK5EN-90 in --mheard afterwards.
    peer_heard = None
    if args.peer_port:
        try:
            sys.path.insert(0, str(Path(__file__).resolve().parent))
            from tdeck_harness import TDeckSession  # noqa: E402
            peer = TDeckSession(port=args.peer_port, boot_timeout=40.0, ready_timeout=45.0)
            peer.probe_cmd = "--oledstat"; peer.probe_pattern = r"\[OLEDSTAT\]"; peer.wake_cmd = None
            peer.open()
            pidx = peer.send("--mheard")
            peer.wait_for(r"MHeard|MH", 4.0, since=pidx)
            time.sleep(1.5)
            peer_heard = any(re.search(r"\bDK5EN-90\b", l) for _, _, l in peer.records_since(pidx))
            peer.close()
        except Exception as e:  # noqa: BLE001
            peer_heard = f"error: {e}"
    ok = (m is not None) if peer_heard is None else (peer_heard is True)
    return {"ok": ok, "first_line": m.string.strip()[:120] if m else None,
            "tx_lines": tx_lines[:5], "peer_port": args.peer_port, "peer_heard_us": peer_heard}


def scenario_mheard(s: RakSession, args: argparse.Namespace) -> Dict[str, Any]:
    """--mheard lists the other bench nodes heard on LoRa (proves RX works)."""
    idx = s.send("--mheard")
    s.wait_for(r"MHeard|MH", 4.0, since=idx)
    s.pump(1.5)
    lines = s.lines_since(idx)
    calls = sorted({m.group(0) for l in lines for m in [re.search(r"\bDK5EN-\d+\b", l)] if m})
    return {"ok": len(calls) >= 1, "calls_heard": calls, "lines": len(lines)}


SCENARIOS = {
    "boot": scenario_boot, "info": scenario_info, "instr": scenario_instr,
    "lora": scenario_lora, "mheard": scenario_mheard,
}
ORDER = ["boot", "info", "instr", "lora", "mheard"]
HELP = {
    "boot": "--reboot, host-timestamped boot phases to [BOOT];ready, crash markers, boot count",
    "info": "--info answers; call sign, hardware, gateway state",
    "instr": "loop max / heap / per-section stalls over a quiet window (TM-13)",
    "lora": "--sendpos produces a LoRa TX",
    "mheard": "--mheard shows at least one other bench node",
}


def print_summary(summary: Dict[str, Dict[str, Any]]) -> None:
    for name, r in summary.items():
        print(f"[{name}] {'PASS' if r.get('ok') else 'FAIL'}")
        if name == "boot":
            print(f"  port gone after {r.get('port_gone_after_s')} s, boots={r.get('boot_count')} double_boot={r.get('double_boot')}")
            for b in r.get("boots", []):
                print("   " + "  ".join(f"{k}={v}" for k, v in b.items()))
            if r.get("crash_lines"):
                print(f"  CRASH: {r['crash_lines']}")
        elif name == "info":
            print(f"  lines={r.get('answered_lines')} call={r.get('call_line')} hw={r.get('hw_line')} gw={r.get('gateway_line')}")
        elif name == "instr":
            print(f"  {r.get('seconds')}s: loop n={r.get('loop_n')} avg_us={r.get('loop_avg_us')} max_us={r.get('loop_max_us')} gaps={r.get('gaps')} worst={r.get('worst_section')} heap_free={r.get('heap_free')} largest={r.get('heap_largest')}")
            for k, v in sorted(r.get("sections", {}).items(), key=lambda kv: -(kv[1]["max_us"] or 0)):
                print(f"    {k:14s} n={v['n']} avg_us={v['avg_us']} max_us={v['max_us']}")
            for g in r.get("gap_events", []):
                print(f"    {g.strip()[:110]}")
        elif name == "lora":
            print(f"  first={r.get('first_line')} tx_lines={len(r.get('tx_lines', []))} peer={r.get('peer_port') or '-'} peer_heard_us={r.get('peer_heard_us')}")
        elif name == "mheard":
            print(f"  heard={r.get('calls_heard')}")


def main(argv: Optional[Sequence[str]] = None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--scenario", default="all")
    p.add_argument("--skip", default="")
    p.add_argument("--list", action="store_true")
    p.add_argument("--port", default=DEFAULT_PORT)
    p.add_argument("--boot-seconds", type=float, default=60.0, help="capture window after --reboot (default 60)")
    p.add_argument("--instr-seconds", type=float, default=60.0, help="quiet window of the instr scenario (default 60)")
    p.add_argument("--loop-budget-us", type=int, default=2000000, help="loop max budget (default 2 s; the RAK paces at ~100 ms)")
    p.add_argument("--peer-port", default="", help="lora: ESP32 bench node whose --mheard must list DK5EN-90 (reboots that node)")
    p.add_argument("--out", default="rak_summary.json")
    args = p.parse_args(argv)
    if args.list:
        for n in ORDER:
            print(f"  {n:8s} {HELP[n]}")
        return 0
    wanted = ORDER if args.scenario == "all" else [x.strip() for x in args.scenario.split(",") if x.strip()]
    skip = {x.strip() for x in args.skip.split(",") if x.strip()}
    bad = [x for x in wanted + sorted(skip) if x not in SCENARIOS]
    if bad:
        print(f"unknown scenario(s): {bad}", file=sys.stderr)
        return 2
    s = RakSession(args.port)
    try:
        s.open()
    except Exception as e:  # noqa: BLE001
        print(f"ERROR: {e}", file=sys.stderr)
        return 2
    s.pump(1.0)
    summary: Dict[str, Dict[str, Any]] = {}
    ok = True
    try:
        for name in [x for x in wanted if x not in skip]:
            print(f"=== running scenario: {name} ===", file=sys.stderr)
            r = SCENARIOS[name](s, args)
            summary[name] = r
            ok = ok and bool(r.get("ok"))
    finally:
        s.close()
    print_summary(summary)
    with open(args.out, "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2, default=str)
    print(f"summary written to {args.out}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
