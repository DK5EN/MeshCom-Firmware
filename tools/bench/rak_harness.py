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

sys.path.insert(0, str(Path(__file__).resolve().parent))
import extudp_peer as ep  # noqa: E402  (host end of the EXTUDP link, TM-43)

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


def dtr_for(port: str) -> bool:
    """DTR default per port family (TM-43, so the same session serves both nodes).

    The RAK's own USB CDC (`/dev/cu.usbmodem*`) stays silent without DTR and does
    NOT reset when the port is opened; an ESP32 bench node behind a USB-UART
    bridge (`/dev/cu.usbserial*`) is the other way round -- it resets on open and
    must be left with DTR/RTS low (memory rak4631-serial-testing-pitfalls,
    bench-fleet-ports).
    """
    return "usbmodem" in os.path.basename(port)


def settle_for(port: str) -> float:
    """Seconds to wait after opening before the node answers commands.

    A node that reboots when its port is opened needs its whole boot; the RAK
    is already running and answers immediately.
    """
    return 1.0 if dtr_for(port) else 18.0


class RakSession:
    """Held-open serial session with a host-timestamped line log."""

    def __init__(self, port: str, log_path: Optional[Path] = None,
                 dtr: Optional[bool] = None) -> None:
        self.port = port
        self.dtr = dtr_for(port) if dtr is None else dtr
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
                    s.dtr = self.dtr  # the RAK stays silent without DTR; an ESP32 resets with it
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


# =====================================================================
# TM-43 / UDP-01: the EXTUDP transport, both directions, on real hardware
# =====================================================================
#
# The assertion that matters is LIVENESS: datagrams flying is the easy half,
# the reported failure is the node dying while they do (N-22 crashed 2-4 s
# after a send, N-23 froze the loop on the first pass). Everything below is
# therefore wrapped in a start/end uptime comparison, a boot-line watch and a
# console echo probe -- plus the [EXT];rx / [EXT];tx stack watermark lines the
# fork-only instrument in extudp_functions.cpp prints on both platforms.

EXT_STARTED = r"\[EXT\]\.\.\.now (listening|sending)|\[EXT\] to IP"
EXT_INC = r"\[EXT\] Inc:"
EXT_OUT = r"\[EXT\] Out:"
EXT_HWM = re.compile(r"\[EXT\];(rx|tx);len;(-?\d+);stack_hwm;(\d+);ms;(\d+)")
POS_SUPPRESSED = r"\[POS\];shot;suppressed"
# Console evidence that a frame reached the TX ring / the send path. RING_WRITE
# and OnTXDone need --debug on; [BP];notice carries the ring depth and is a raw
# printf, so it survives a node with debug off.
TX_EVIDENCE = r"OnTXDone|TX-LoRa|RING_WRITE|\[BP\];(notice|refuse)"
BOOTED = r"\[BOOT\]|RESETREAS="
SHOT_FLOOR_S = 30.0        # FL-01: the "send now" beacon has a 30 s floor


def _info(s: RakSession, timeout: float = 8.0) -> Dict[str, Any]:
    """One --info round trip, parsed into the fields TM-43 needs.

    `extudp` / `ext_ip` are read BEFORE anything is changed and written back
    verbatim at the end of the run -- the node must leave the test in exactly
    the state it entered it.
    """
    idx = s.send("--info")
    s.wait_for(r"--MeshCom|\.\.\.Call:", timeout, since=idx)
    s.pump(2.0)
    lines = s.lines_since(idx)
    blob = "\n".join(lines)

    def grp(pat: str, cast: Any = str) -> Any:
        m = re.search(pat, blob)
        return cast(m.group(1)) if m else None

    # "--extudpip none" clears the field, so --info prints EXT IP with nothing
    # after it: the address group must be allowed to be empty.
    ext = re.search(r"\.\.\.EXTUDP (on|off) \.\.\.EXT IP ?(\S*)", blob)
    return {
        "answered_lines": len(lines),
        "call": grp(r"\.\.\.Call: <([^>]+)>"),
        "uptime_ms": grp(r"\.\.\.TIME (\d+) ms", int),
        "ip": grp(r"IP address\s*:\s*([\d.]+)"),
        "extudp": (ext.group(1) == "on") if ext else None,
        "ext_ip": ext.group(2) if ext else None,
        "gateway": grp(r"Gateway (on|off)"),
        "debug": grp(r"\.\.\.DEBUG (on|off) \.\.\.LORADEBUG"),
        "build": grp(r"--MeshCom \S+ \(build: ([^)]+)\)"),
    }


def _pos(s: RakSession, timeout: float = 6.0) -> Dict[str, Any]:
    """--pos: the node's own position, and whether it has one at all.

    A node with LAT/LON 0.0000 (no GPS, no fixed position) never reaches the
    EXTUDP path from sendPosition(): PositionToAPRS() returns an empty payload
    and sendPosition() returns before sendExtern() (loop_functions.cpp:4374).
    Both --sendpos and the telemetry push beacon are then inapplicable on that
    node -- which is a property of its configuration, not an EXTUDP defect.
    """
    idx = s.send("--pos")
    s.wait_for(r"\.\.\.LAT:", timeout, since=idx)
    s.pump(1.5)
    blob = "\n".join(s.lines_since(idx))
    lat = re.search(r"\.\.\.LAT:\s*(-?[\d.]+)", blob)
    lon = re.search(r"\.\.\.LON:\s*(-?[\d.]+)", blob)
    flat = float(lat.group(1)) if lat else 0.0
    flon = float(lon.group(1)) if lon else 0.0
    gps = re.search(r"\.\.\.GPS: (\w+)", blob)
    return {"lat": flat, "lon": flon, "has_position": abs(flat) > 0.0 or abs(flon) > 0.0,
            "gps": gps.group(1) if gps else None}


def _reboot(s: RakSession, wait: float = 60.0) -> bool:
    """--reboot, surviving a USB re-enumeration (RAK) or not (ESP32 bridge).

    The RAK's CDC port vanishes and comes back; a USB-UART bridge keeps the
    port open across the node's reset. Both are handled by watching whether
    the port drops rather than by board type.
    """
    s.send("--reboot")
    deadline = time.monotonic() + wait
    idx = len(s.lines)
    while time.monotonic() < deadline:
        if s.ser is None or not s.pump(0.5):
            s.close()
            while time.monotonic() < deadline and os.path.exists(s.port):
                time.sleep(0.2)
            try:
                s.open(wait_for_node=max(1.0, deadline - time.monotonic()))
            except TimeoutError:
                return False
            idx = len(s.lines)
            continue
        if any(re.search(r"\[BOOT\]|CLIENT STARTED|RUNNING", l) for l in s.lines_since(idx)):
            s.pump(3.0)
            return True
    return False


def _hwm_lines(lines: Sequence[str]) -> Dict[str, Dict[str, Any]]:
    """[EXT];rx/tx;len;N;stack_hwm;W;ms;T -> per direction min/max/count.

    The minimum is the answer UDP-01 asks for: how close the deepest EXTUDP
    path came to the bottom of the 4 KB nRF52 loop-task stack. nRF52/FreeRTOS
    reports the watermark in WORDS (x4 = bytes), ESP32 in bytes.
    """
    out: Dict[str, Dict[str, Any]] = {}
    for l in lines:
        m = EXT_HWM.search(l)
        if not m:
            continue
        d = out.setdefault(m.group(1), {"n": 0, "min": None, "max": None, "len_max": 0})
        w = int(m.group(3))
        d["n"] += 1
        d["min"] = w if d["min"] is None else min(d["min"], w)
        d["max"] = w if d["max"] is None else max(d["max"], w)
        d["len_max"] = max(d["len_max"], int(m.group(2)))
    return out


def _wait_shot_floor(s: RakSession, idx: int) -> bool:
    """True if FL-01 suppressed the shot beacon since `idx` (then it is worth waiting)."""
    return any(re.search(POS_SUPPRESSED, l) for l in s.lines_since(idx))


def scenario_extudp(s: RakSession, args: argparse.Namespace) -> Dict[str, Any]:
    """TM-43: EXTUDP send + receive + rejection + liveness + soak, on hardware.

    Host binds UDP 1799, points the node at it (--extudpip <host>, --extudp on,
    rebooting only if the socket does not come up -- startExternUDP() returns
    early while hasExternIPaddress is still true from an earlier peer), then:

      send    --sendpos / ::{TEST}... / a LoRa frame from --peer-port
              => one JSON datagram per trigger AND the matching [EXT] Out: line.
              [EXT] Out: without a datagram is the N-22 side symptom: FAIL.
      receive {"type":"msg"} => TX-ring evidence, {"type":"tele"} => beacon
              evidence or the documented refusal on a node with real sensors.
      reject  the seven malformed vectors of extudp_peer.rejection_vectors():
              each must be logged and the node must still answer afterwards.
      live    uptime monotonic, no [BOOT]/RESETREAS after the start, no crash
              marker, console still echoing.
      soak    --soak-seconds of traffic in both directions, then the same
              liveness check plus the heartbeat sequence-gap check.
    """
    res: Dict[str, Any] = {"ok": False, "port": s.port}
    start_idx = len(s.lines)

    pre = _info(s)
    res["pre_state"] = pre
    pos = _pos(s)
    res["position"] = pos
    if pre.get("ip") is None or pre.get("extudp") is None:
        res["error"] = "node did not answer --info with an IP / EXTUDP state"
        return res
    node_ip = str(pre["ip"])
    host_ip = args.host_ip or ep.detect_host_ip(node_ip) or ""
    res["node_ip"] = node_ip
    res["host_ip"] = host_ip
    if not host_ip:
        res["error"] = "could not determine the host IP for --extudpip"
        return res

    peer = ep.ExtUdpPeer(bind_port=args.ext_port)
    try:
        peer.start()
    except OSError as e:  # noqa: BLE001
        res["error"] = f"cannot bind UDP {args.ext_port}: {e}"
        return res

    marker = int(time.time()) % 100000
    try:
        # ---- bring the socket up ------------------------------------
        s.send("--debug on")
        s.pump(0.5)
        start_pidx = peer.mark()
        cfg_idx = s.send(f"--extudpip {host_ip}")
        s.pump(1.0)
        s.send("--extudp on")
        # Either the startExternUDP() banner, or -- for a node that was already
        # pointed at this host -- a datagram from it: startExternUDP() prints
        # its banner once and then returns early while hasExternIPaddress is
        # true, so a live socket is proven by traffic, not by the banner. Both
        # are watched in one window to save a needless reboot.
        def socket_up(deadline: float) -> Optional[Any]:
            while True:
                m = s.wait_for(EXT_STARTED, 0.4, since=cfg_idx)
                if m is not None:
                    return m
                if any(d.addr[0] == node_ip for d in peer.since(start_pidx)):
                    res["ext_started_note"] = "socket already up (datagram received from the node)"
                    return re.match(r"(.*)", "already up")
                if time.monotonic() >= deadline:
                    return None

        started = socket_up(time.monotonic() + args.ext_start_timeout)
        rebooted = False
        if started is None or (pre["ext_ip"] != host_ip and pre["extudp"]):
            # The node was already pointed at another peer: startExternUDP()
            # bails out while hasExternIPaddress is true, so the new address
            # only takes effect after a restart. N-23 order trap: gateway and
            # Ethernet/WiFi come up first, EXTUDP after -- which is exactly
            # what the boot path does.
            rebooted = _reboot(s)
            s.send("--debug on")
            s.pump(0.5)
            cfg_idx, start_pidx = len(s.lines), peer.mark()
            started = socket_up(time.monotonic() + args.ext_start_timeout)
        res["ext_started"] = started.string.strip()[:120] if started else None
        res["rebooted_to_apply"] = rebooted
        if started is None:
            res["error"] = "EXTUDP socket never came up ([EXT]...now listening missing)"
            return res
        s.pump(2.0)
        # Liveness baseline: taken AFTER the optional bring-up reboot, so the
        # uptime and boot-line checks below cover the test itself and nothing
        # else. Every [BOOT]/RESETREAS line from here on is unexpected.
        base_idx = len(s.lines)
        base = _info(s)
        res["baseline_uptime_ms"] = base.get("uptime_ms")

        # ---- send direction ------------------------------------------
        def wait_dg(match: Any, timeout: float, since: int) -> Optional[Any]:
            """Wait for a datagram while KEEPING THE CONSOLE PUMPED.

            Not draining the serial port for a whole trigger window loses
            console lines (the node's USB CDC buffer is small) -- and the
            [EXT] Out: line that a datagram must be paired with is exactly
            what would go missing, turning a healthy node into a fake N-22.
            """
            end = time.monotonic() + timeout
            while True:
                hit = next((d for d in peer.since(since) if match(d)), None)
                if hit is not None:
                    return hit
                if time.monotonic() >= end:
                    return None
                s.pump(0.2)

        def one_trigger(name: str, fire: Any, match: Any, timeout: float,
                        retry_on_shot_floor: bool = False) -> Dict[str, Any]:
            pidx, lidx = peer.mark(), len(s.lines)
            fire()
            dg = wait_dg(match, timeout, pidx)
            s.pump(0.5)
            if dg is None and retry_on_shot_floor and _wait_shot_floor(s, lidx):
                # FL-01 rate-limits the shot beacon to one per 30 s; wait out
                # the floor once instead of reporting a transport failure.
                s.pump(SHOT_FLOOR_S + 2.0)
                pidx, lidx = peer.mark(), len(s.lines)
                fire()
                dg = wait_dg(match, timeout, pidx)
                s.pump(0.5)
            outs = [l for l in s.lines_since(lidx) if re.search(EXT_OUT, l)]
            item = {
                "trigger": name,
                "datagram": (dg.text[:200] if dg else None),
                "datagram_bytes": (len(dg.raw) if dg else 0),
                "out_lines": len(outs),
                "suppressed_by_fl01": _wait_shot_floor(s, lidx),
            }
            item["ok"] = dg is not None and len(outs) >= 1
            if dg is None and outs:
                item["fail_reason"] = "[EXT] Out: logged but no datagram arrived (N-22 symptom)"
            elif dg is None:
                item["fail_reason"] = "no datagram"
            elif not outs:
                item["fail_reason"] = "datagram arrived but no [EXT] Out: console line"
            return item

        sends: List[Dict[str, Any]] = []
        if pos.get("has_position"):
            sends.append(one_trigger(
                "sendpos", lambda: s.send("--sendpos"),
                lambda d: ep.is_out_datagram(d) and d.get("src_type") == "node"
                and d.get("type") == "pos",
                args.ext_trigger_timeout, retry_on_shot_floor=True))
        else:
            sends.append({
                "trigger": "sendpos", "ok": True, "skipped": True,
                "fail_reason": f"node has no position (LAT {pos.get('lat')} LON {pos.get('lon')}, "
                               f"GPS {pos.get('gps')}): sendPosition() returns before sendExtern(), "
                               f"so this trigger cannot reach the EXTUDP path on this node",
            })

        own_txt = f"tm43 {marker}"
        sends.append(one_trigger(
            "own_msg", lambda: s.send(f"::{{TEST}}{own_txt}"),
            lambda d: ep.is_out_datagram(d) and d.get("src_type") == "node"
            and own_txt in str(d.get("msg", "")),
            args.ext_trigger_timeout))

        peer_call: Optional[str] = None
        if args.peer_port:
            lora_txt = f"tm43lora {marker}"
            other = RakSession(args.peer_port, log_path=Path(str(s.log_path) + ".peer"))
            try:
                other.open()
                other.pump(settle_for(args.peer_port))
                pinfo = _info(other)
                peer_call = pinfo.get("call")
                sends.append(one_trigger(
                    "lora_from_peer", lambda: other.send(f"::{{TEST}}{lora_txt}"),
                    lambda d: ep.is_out_datagram(d) and d.get("src_type") == "lora"
                    and (peer_call is None or d.get("src") == peer_call)
                    and lora_txt in str(d.get("msg", "")),
                    args.ext_trigger_timeout + 20.0))
            except Exception as e:  # noqa: BLE001
                sends.append({"trigger": "lora_from_peer", "ok": False,
                              "fail_reason": f"peer error: {e}"})
            finally:
                other.close()
        res["peer_call"] = peer_call
        res["send"] = sends

        # ---- receive direction ---------------------------------------
        recv: List[Dict[str, Any]] = []

        rx_txt = f"tm43 rx {marker}"
        lidx, pidx = len(s.lines), peer.mark()
        peer.send({"type": "msg", "dst": "TEST", "msg": rx_txt}, node_ip, args.ext_port)
        s.wait_for(EXT_INC, 10.0, since=lidx)
        # The node re-serializes its own outgoing frame onto the same socket
        # (sendMessage() -> sendExtern(..., "node", ...), loop_functions.cpp:3798):
        # that echo is the proof the inbound datagram reached the send path,
        # and it is visible from the host, not only on the console.
        echo = wait_dg(lambda d: ep.is_out_datagram(d) and d.get("src_type") == "node"
                       and rx_txt in str(d.get("msg", "")), 15.0, pidx)
        s.pump(2.0)
        got = s.lines_since(lidx)
        recv.append({
            "case": "msg_to_txring",
            "inc_line": next((l.strip()[:120] for l in got if re.search(EXT_INC, l)), None),
            "echo_datagram": (echo.text[:160] if echo else None),
            "tx_evidence": [l.strip()[:110] for l in got if re.search(TX_EVIDENCE, l)][:3],
            "hwm": _hwm_lines(got).get("rx"),
            "ok": any(re.search(EXT_INC, l) for l in got)
            and (echo is not None or any(re.search(TX_EVIDENCE, l) for l in got)),
        })

        def fire_tele() -> Tuple[List[str], Optional[Any], int]:
            li, pi = len(s.lines), peer.mark()
            peer.send({"type": "tele", "temp": 23.3, "hum": 60, "press": 1018.5},
                      node_ip, args.ext_port)
            s.wait_for(r"\[EXT\] tele (accepted|ignored|missing)", 10.0, since=li)
            b = wait_dg(lambda d: ep.is_out_datagram(d) and d.get("src_type") == "node"
                        and d.get("type") in ("pos", "tele"), 12.0, pi)
            s.pump(1.0)
            return s.lines_since(li), b, li

        got, beacon, lidx = fire_tele()
        suppressed = any(re.search(POS_SUPPRESSED, l) for l in got)
        if beacon is None and suppressed and pos.get("has_position"):
            # FL-01's 30 s floor swallowed the push beacon -- wait it out once
            # so the beacon evidence is real rather than a timing artefact.
            s.pump(SHOT_FLOOR_S + 3.0)
            got2, beacon, lidx = fire_tele()
            got = got + got2
        accepted = any("tele accepted" in l for l in got)
        refused = any("tele ignored: real sensor hardware" in l for l in got)
        recv.append({
            "case": "tele",
            "accepted": accepted, "refused_real_sensors": refused,
            "line": next((l.strip()[:140] for l in got if "[EXT] tele" in l), None),
            "beacon_datagram": (beacon.text[:160] if beacon else None),
            "beacon_suppressed_by_fl01": suppressed,
            "beacon_inapplicable_no_position": not pos.get("has_position"),
            # A refusal is a pass in its own right (documented behaviour on a
            # node with real sensor hardware). Where the values ARE taken, the
            # push beacon proves it -- or FL-01's suppression line does, which
            # is equally proof that sendPosition() was reached. On a node
            # without a position there is no beacon to see at all (see _pos());
            # the accepted line with the values echoed back is then the whole
            # of what the firmware offers.
            "ok": refused or (accepted and (beacon is not None or suppressed
                                            or not pos.get("has_position"))),
        })
        res["receive"] = recv

        # ---- rejection vectors ---------------------------------------
        rejects: List[Dict[str, Any]] = []
        for name, vec in ep.rejection_vectors():
            got: List[str] = []
            attempts = 0
            # UDP is lossy (the ESP32 control is on WiFi): a vector that never
            # reached the node proves nothing, so it is retried once. A vector
            # that ARRIVED and was not logged is the real failure.
            while attempts < 2:
                attempts += 1
                lidx = len(s.lines)
                peer.send(vec, node_ip, args.ext_port)
                s.wait_for(EXT_INC, 8.0, since=lidx)
                s.pump(1.5)
                got = s.lines_since(lidx)
                if any(re.search(EXT_INC, l) for l in got):
                    break
            logged = [l.strip()[:120] for l in got
                      if re.search(r"deserializeJson\(\) failed|missing dst/msg|invalid lengths"
                                   r"|wrong JSON|\[EXT\] tele missing", l)]
            rejects.append({
                "vector": name, "bytes": len(vec), "attempts": attempts,
                "seen_by_node": any(re.search(EXT_INC, l) for l in got),
                "log": logged[:2],
                "ok": any(re.search(EXT_INC, l) for l in got) and bool(logged),
            })
        # After the whole barrage the node must still answer on BOTH channels:
        # the console (uptime) and -- the check that catches the ESP32 wedge --
        # the UDP socket itself. A rejected probe datagram must still be seen
        # and logged; if it is not, the inbound path died on one of the vectors
        # above (on ESP32 the 255-byte one does exactly that, permanently).
        lidx = len(s.lines)
        peer.send(ep.ALIVE_PROBE, node_ip, args.ext_port)
        alive = s.wait_for(EXT_INC, 12.0, since=lidx)
        if alive is None:      # UDP is lossy: one retry before calling it dead
            lidx = len(s.lines)
            peer.send(ep.ALIVE_PROBE, node_ip, args.ext_port)
            alive = s.wait_for(EXT_INC, 12.0, since=lidx)
        post_reject = _info(s)
        res["reject"] = rejects
        res["inbound_alive_after_vectors"] = alive is not None
        res["echo_after_reject"] = post_reject.get("uptime_ms") is not None

        # ---- soak tail ------------------------------------------------
        soak_idx = len(s.lines)
        soak_pidx = peer.mark()
        t0 = time.monotonic()
        n_in = n_msg = 0
        last_pos = 0.0
        vectors = ep.rejection_vectors()
        while time.monotonic() - t0 < args.soak_seconds:
            # Every fourth datagram is a real message (the deep path:
            # getExtern -> sendMessage -> TX ring -> sendExtern); the others
            # are malformed and stop inside the parser. That keeps the inbound
            # path under constant load without putting a LoRa frame on the air
            # every three seconds.
            if n_in % 4 == 0:
                peer.send({"type": "msg", "dst": "TEST", "msg": f"soak {n_msg} {marker}"},
                          node_ip, args.ext_port)
                n_msg += 1
            else:
                peer.send(vectors[n_in % len(vectors)][1], node_ip, args.ext_port)
            n_in += 1
            if time.monotonic() - last_pos > SHOT_FLOOR_S + 5.0:
                s.send("--sendpos")           # FL-01 floor: not more often than that
                last_pos = time.monotonic()
            s.pump(args.soak_interval)
        soak_lines = s.lines_since(soak_idx)
        soak_dgs = peer.since(soak_pidx)
        hb = peer.heartbeats(soak_pidx)
        res["soak"] = {
            "seconds": round(time.monotonic() - t0, 1),
            "datagrams_sent_to_node": n_in,
            "of_which_valid_messages": n_msg,
            "datagrams_from_node": sum(1 for d in soak_dgs if ep.is_out_datagram(d)),
            "console_lines": len(soak_lines),
            # How many of the datagrams the node actually picked up: zero here
            # with traffic still going out means the inbound path is wedged
            # while the node looks alive from the outside.
            "inbound_seen": sum(1 for l in soak_lines if re.search(EXT_INC, l)),
            # MC_TEST_HOOKS is not defined in the stock build: no heartbeat is
            # not a failure, a GAP in one is.
            "heartbeats": len(hb),
            "heartbeat_gaps": peer.heartbeat_gaps(soak_pidx),
            "boot_lines": [l.strip()[:100] for l in soak_lines if re.search(BOOTED, l)][:5],
        }

        # ---- liveness -------------------------------------------------
        post = _info(s)
        run_lines = s.lines_since(base_idx)
        crash = [l.strip()[:120] for l in run_lines if re.search(CRASH, l)]
        boots = [l.strip()[:100] for l in run_lines if re.search(BOOTED, l)]
        uptime_ok = (post.get("uptime_ms") is not None and base.get("uptime_ms") is not None
                     and post["uptime_ms"] > base["uptime_ms"])
        res["hwm"] = _hwm_lines(s.lines_since(start_idx))
        res["live"] = {
            "uptime_start_ms": base.get("uptime_ms"), "uptime_end_ms": post.get("uptime_ms"),
            "uptime_monotonic": uptime_ok, "crash_lines": crash,
            "unexpected_boot_lines": boots[:5],
            "console_echoes": post.get("uptime_ms") is not None,
            "ok": uptime_ok and not crash and not boots and post.get("uptime_ms") is not None,
        }
        res["post_state"] = post
    finally:
        # ---- restore exactly the state the node was found in ----------
        restore: Dict[str, Any] = {}
        try:
            s.send(f"--extudpip {pre['ext_ip'] if pre.get('ext_ip') not in (None, 'none') else 'none'}")
            s.pump(1.0)
            s.send("--extudp on" if pre.get("extudp") else "--extudp off")
            s.pump(1.0)
            if pre.get("debug") in ("on", "off"):
                s.send(f"--debug {pre['debug']}")
                s.pump(0.5)
            back = _info(s)

            def norm(v: Any) -> str:
                # "none", an empty field and the stray byte a cleared
                # node_extern buffer prints are the same state to the firmware
                # (strlen < 7 disables the socket), and --extudpip none always
                # clears the field -- a node found holding the literal string
                # "none" therefore comes back empty. A real address must match
                # character for character.
                t = re.sub(r"[^A-Za-z0-9.:_-]", "", str(v or ""))
                return "" if t == "none" else t

            same_ip = norm(back.get("ext_ip")) == norm(pre.get("ext_ip"))
            restore = {"extudp": back.get("extudp"), "ext_ip": back.get("ext_ip"),
                       "debug": back.get("debug"),
                       "matches_pre": back.get("extudp") == pre.get("extudp") and same_ip}
        except Exception as e:  # noqa: BLE001
            restore = {"error": str(e), "matches_pre": False}
        res["restore"] = restore
        peer.stop()

    res["ok"] = bool(
        all(x.get("ok") for x in res.get("send", [])) and res.get("send")
        and all(x.get("ok") for x in res.get("receive", []))
        and all(x.get("ok") for x in res.get("reject", []))
        and res.get("inbound_alive_after_vectors")
        and res.get("echo_after_reject")
        and res.get("soak", {}).get("inbound_seen", 0) > 0
        and res.get("live", {}).get("ok")
        and not res.get("soak", {}).get("heartbeat_gaps")
        and not res.get("soak", {}).get("boot_lines")
        and res.get("restore", {}).get("matches_pre")
    )
    return res


SCENARIOS = {
    "boot": scenario_boot, "info": scenario_info, "instr": scenario_instr,
    "lora": scenario_lora, "mheard": scenario_mheard, "extudp": scenario_extudp,
}
# `--scenario all` runs ORDER. extudp is deliberately NOT in it: it reconfigures
# the node, may reboot it, and carries a ten-minute soak tail by default -- it is
# asked for by name (`--scenario extudp`), not swept up by "all".
ORDER = ["boot", "info", "instr", "lora", "mheard"]
EXTRA = ["extudp"]
HELP = {
    "boot": "--reboot, host-timestamped boot phases to [BOOT];ready, crash markers, boot count",
    "info": "--info answers; call sign, hardware, gateway state",
    "instr": "loop max / heap / per-section stalls over a quiet window (TM-13)",
    "lora": "--sendpos produces a LoRa TX",
    "mheard": "--mheard shows at least one other bench node",
    "extudp": "TM-43: UDP 1799 both directions, rejection vectors, liveness + soak (long)",
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
        elif name == "extudp":
            if r.get("error"):
                print(f"  ERROR: {r['error']}")
            pre, post = r.get("pre_state", {}), r.get("restore", {})
            po = r.get("position", {})
            print(f"  node={pre.get('call')} {r.get('node_ip')} host={r.get('host_ip')} "
                  f"build={pre.get('build')} rebooted={r.get('rebooted_to_apply')} "
                  f"pos={po.get('lat')},{po.get('lon')} gps={po.get('gps')}")
            print(f"  {'trigger':16s} {'ok':5s} {'out':4s} {'bytes':6s} datagram / reason")
            for t in r.get("send", []):
                verdict = "SKIP" if t.get("skipped") else ("PASS" if t.get("ok") else "FAIL")
                print(f"  {t.get('trigger',''):16s} {verdict:5s} "
                      f"{str(t.get('out_lines','-')):4s} {str(t.get('datagram_bytes','-')):6s} "
                      f"{(t.get('datagram') or t.get('fail_reason') or '')[:70]}")
            for c in r.get("receive", []):
                extra = c.get("tx_evidence") or c.get("line") or ""
                print(f"  {c.get('case',''):16s} {'PASS' if c.get('ok') else 'FAIL':5s} {str(extra)[:80]}")
            for v in r.get("reject", []):
                print(f"  {v.get('vector',''):16s} {'PASS' if v.get('ok') else 'FAIL':5s} "
                      f"{v.get('bytes')} B seen={v.get('seen_by_node')} {str(v.get('log'))[:70]}")
            hwm = r.get("hwm", {})
            for d in ("rx", "tx"):
                h = hwm.get(d)
                if h:
                    print(f"  stack_hwm {d}: min={h['min']} max={h['max']} n={h['n']} "
                          f"len_max={h['len_max']}   (nRF52: words, x4 = bytes; ESP32: bytes)")
            print(f"  {'inbound_alive':16s} {'PASS' if r.get('inbound_alive_after_vectors') else 'FAIL':5s} "
                  f"probe datagram after the vector barrage")
            sk = r.get("soak", {})
            if sk:
                print(f"  soak {sk.get('seconds')}s: in={sk.get('datagrams_sent_to_node')} "
                      f"(msgs {sk.get('of_which_valid_messages')}) seen_by_node={sk.get('inbound_seen')} "
                      f"out={sk.get('datagrams_from_node')} hb={sk.get('heartbeats')} "
                      f"gaps={sk.get('heartbeat_gaps')} boots={sk.get('boot_lines')}")
            lv = r.get("live", {})
            print(f"  live: uptime {lv.get('uptime_start_ms')} -> {lv.get('uptime_end_ms')} ms "
                  f"monotonic={lv.get('uptime_monotonic')} crash={lv.get('crash_lines')} "
                  f"boots={lv.get('unexpected_boot_lines')} echo={lv.get('console_echoes')}")
            print(f"  restore: extudp={post.get('extudp')} ext_ip={post.get('ext_ip')} "
                  f"matches_pre={post.get('matches_pre')}")


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
    p.add_argument("--dtr", choices=["auto", "on", "off"], default="auto",
                   help="serial DTR: auto = on for /dev/cu.usbmodem* (RAK), off for a USB-UART bridge")
    p.add_argument("--host-ip", default="", help="extudp: address to feed --extudpip (default: auto-detected)")
    p.add_argument("--ext-port", type=int, default=ep.EXTERN_PORT, help="extudp: UDP port (default 1799)")
    p.add_argument("--ext-start-timeout", type=float, default=25.0,
                   help="extudp: wait for [EXT]...now listening before rebooting the node (default 25)")
    p.add_argument("--ext-trigger-timeout", type=float, default=25.0,
                   help="extudp: per-trigger wait for the datagram (default 25)")
    p.add_argument("--soak-seconds", type=float, default=600.0, help="extudp: soak tail (default 600)")
    p.add_argument("--soak-interval", type=float, default=3.0, help="extudp: seconds between soak datagrams")
    p.add_argument("--out", default="rak_summary.json")
    args = p.parse_args(argv)
    if args.list:
        for n in ORDER:
            print(f"  {n:8s} {HELP[n]}")
        for n in EXTRA:
            print(f"  {n:8s} {HELP[n]}  [not in --scenario all]")
        return 0
    wanted = ORDER if args.scenario == "all" else [x.strip() for x in args.scenario.split(",") if x.strip()]
    skip = {x.strip() for x in args.skip.split(",") if x.strip()}
    bad = [x for x in wanted + sorted(skip) if x not in SCENARIOS]
    if bad:
        print(f"unknown scenario(s): {bad}", file=sys.stderr)
        return 2
    s = RakSession(args.port, dtr=None if args.dtr == "auto" else (args.dtr == "on"))
    try:
        s.open()
    except Exception as e:  # noqa: BLE001
        print(f"ERROR: {e}", file=sys.stderr)
        return 2
    # A node behind a USB-UART bridge reboots when the port is opened; give it
    # its boot before the first command instead of talking into a resetting node.
    s.pump(settle_for(args.port))
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
