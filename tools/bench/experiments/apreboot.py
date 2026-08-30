#!/usr/bin/env python3
"""TM-38 AP-reboot recovery test: all four bench nodes log locally and
unattended while the operator power-cycles the access points.

The bench Mac sits on the same APs, so the machine loses WiFi and Internet for
the ~2 minutes of the reboot and every interactive session on it (ssh, the
Claude Code harness, the 2323 net console) dies with it.  The runner therefore
detaches into a session of its own (double fork + setsid, stdio to files, pid
file), talks USB serial only, uses no network at all, and does all the
reporting from the raw logs afterwards.

    # arm it, then walk to the APs
    python3 tools/bench/experiments/apreboot.py start --label ap1 \
        --board tdeck=/dev/cu.usbmodem1101 \
        --board heltec=/dev/cu.usbserial-0001 \
        --board tbeam=/dev/cu.usbserial-573C0005841 \
        --board rak=/dev/cu.usbmodem201301

    python3 tools/bench/experiments/apreboot.py status   # newest run
    python3 tools/bench/experiments/apreboot.py stop     # kill the runner
    python3 tools/bench/experiments/apreboot.py report tools/bench/runs/apreboot_ap1_*/

Phases (all timing on the host clock -- the boards' uptimes differ):

  SETTLE       every board must reach link up and an IP.  Everything else the
               board does in this window (UDP tx, UDP rx, NTP) becomes its
               *baseline*: whatever it demonstrated before the outage has to
               come back after it.  One `--udplog on` per ESP32 board is the
               only thing ever sent, and it is sent in this phase.  An ESP32
               node that sends no [UDP];tx here is not a gateway (KEEP goes out
               only with --gateway on), which leaves the UDP half of the test
               unexercised: a WARN by default, a settle abort with --strict-udp.
  PROMPT       a `PROMPT` file, a macOS notification and a spoken line tell the
               operator to power-cycle the APs.  The first link-down edge on
               any board is t0.  No edge inside --cycle-window: FAIL, "no
               outage detected".
  RECOVERY     per board, assert the return of link up, IP, and every baseline
               marker, each timestamped relative to t0; assert no reboot and no
               serial command.  All markers inside --recovery: board PASS.

Markers consumed (all raw Serial.printf, so ';' survives --debug off).  The
[WIFI] ones are imported from wifisoak.py rather than copied:

  [WIFI];event;connected|got_ip;ms;N          udp_functions.cpp:717/724
  [WIFI];event;disconnected;reason;R;ms;N     udp_functions.cpp:713
  [WIFI];link;up;rssi;R;...;ip;I;ms;N         udp_functions.cpp:1169 (60 s)
  [WIFI];link;down;sta;S;status;...;ms;N      udp_functions.cpp:1177
  [WIFI];watchdog;reconnect|reset;down_s;N    udp_functions.cpp:1254/1261
  [WIFI];stall;<site>;ms;N;task;T
  [NTP];ok;epoch;N;rtt;N                      ntp_async.cpp:145
  [NTP];timeout|txfail|kod;ip;I[;fails;N]     ntp_async.cpp:85/102/123
  [UDP];tx;ip;I;port;P;len;L;ok;O             udp_functions.cpp:553
  [UDP];rx;ip;I;port;P;len;L;head;H           udp_functions.cpp:161
  [ETH];event;link;up|down;ip;I;ms;N          nrf52/nrf_eth.cpp:92
  [ETH];event;got_ip;IP;ms;N                  nrf52/nrf_eth.cpp:984
  [ETH];event;dhcp;rc;R;ms;N                  nrf52/nrf_eth.cpp:1010
  [ETH];link;up|down;...;rx_n;N;...;ms;N      nrf52/nrf_eth.cpp:101 (60 s)
  [BOOT];ready;ms;N;ip;X[;eth;Y]              esp32_main.cpp:1864 / nrf52_main.cpp:1150
  rst:0x..                                    ESP32 ROM, a reset after the first

[UDP];tx does not exist on the RAK4631 and is reported `n/a` there, never as a
failure: the nRF52 UDP path is `NrfETH::sendUDP()`/`getUDP()` in nrf_eth.cpp,
which has no per-datagram print, and `--udplog` is not even in its command
table (measured on DK5EN-90: "...wrong command"), so the RAK is never sent
anything at all over serial.  The RAK's "server
traffic returning" witness is instead the rx_n counter of its 60-second
[ETH];link heartbeat, which this runner turns into synthetic udp_rx events; its
outbound direction is witnessed by [NTP];ok, a round trip on the very same
gateway socket.
"""

from __future__ import annotations

import argparse
import csv
import glob
import json
import os
import re
import signal
import subprocess
import sys
import threading
import time
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional, Sequence, Tuple

try:
    import serial  # pyserial
except ImportError:  # pragma: no cover - parse/report work without it
    serial = None  # type: ignore[assignment]

sys.path.insert(0, str(Path(__file__).resolve().parent))
import wifisoak  # noqa: E402  (sibling script, reused for the [WIFI] regexes)

BAUD = 115200
WALL_FMT = "%Y-%m-%d %H:%M:%S"
RUN_PREFIX = "apreboot"

# --- markers wifisoak does not carry -----------------------------------------

RE_UDP_TX = re.compile(
    r"\[UDP\];tx;ip;(?P<ip>[\d.]+);port;(?P<port>\d+);len;(?P<len>\d+);ok;(?P<ok>\d+)")
RE_UDP_RX = re.compile(
    r"\[UDP\];rx;ip;(?P<ip>[\d.]+);port;(?P<port>\d+);len;(?P<len>-?\d+);head;(?P<head>[0-9A-Fa-f]+)")
RE_UDP_LOG = re.compile(r"\[UDP\];log;(?P<on>\d)")
RE_NTP_OK = re.compile(r"\[NTP\];ok;epoch;(?P<epoch>\d+);rtt;(?P<rtt>\d+)")
RE_NTP_FAIL = re.compile(r"\[NTP\];(?P<what>timeout|txfail|kod);ip;(?P<ip>[\d.]+)")
RE_ETH_EV_LINK = re.compile(
    r"\[ETH\];event;link;(?P<what>up|down);ip;(?P<ip>\d+);ms;(?P<ms>\d+)")
RE_ETH_EV_GOT_IP = re.compile(r"\[ETH\];event;got_ip;(?P<ip>[\d.]+);ms;(?P<ms>\d+)")
RE_ETH_EV_DHCP = re.compile(r"\[ETH\];event;dhcp;rc;(?P<rc>-?\d+);ms;(?P<ms>\d+)")
RE_ETH_EV_RESET = re.compile(r"\[ETH\];event;reset;ms;(?P<ms>\d+)")
RE_ETH_HB = re.compile(
    r"\[ETH\];(?P<tag>link|stat);(?P<state>up|down);link;(?P<lnk>-?\d+);link_age_s;(?P<age>\d+);"
    r"ip;(?P<ip>[\d.]+);dest;(?P<dest>[^;]*);hb_age_s;(?P<hb>\d+);got_ip_n;(?P<gn>\d+);"
    r"downs;(?P<downs>\d+);renews;(?P<renews>\d+);renew_fail;(?P<rf>\d+);resets;(?P<resets>\d+);"
    r"rx_n;(?P<rx>\d+);rx_max_ms;(?P<rxm>\d+);tx_fail;(?P<txf>\d+);tx_max_ms;(?P<txm>\d+);ms;(?P<ms>\d+)")
RE_ETH_STALL = re.compile(
    r"\[ETH\];stall;(?P<site>[^;]+);ms;(?P<ms>\d+);task;(?P<task>\S+)")

# marker classes asserted after t0, in the order they must appear
MARKERS = ["link_up", "got_ip", "ntp_ok", "udp_tx", "udp_rx"]
# link_up/got_ip are always required; the rest only if the board showed them
# before t0 ("baseline"), so a non-gateway node or a GPS node that never runs
# NTP is not failed for something it never did.
ALWAYS_REQUIRED = ("link_up", "got_ip")

KIND_ESP32 = "esp32"
KIND_RAK = "rak"
# markers the platform cannot emit at all -> reported n/a, never a failure
UNSUPPORTED: Dict[str, Tuple[str, ...]] = {
    KIND_ESP32: (),
    KIND_RAK: ("udp_tx",),   # no per-datagram print in nrf_eth.cpp
}


def wall_now(ts: Optional[float] = None) -> str:
    return time.strftime(WALL_FMT, time.localtime(ts if ts is not None else time.time()))


def wall_to_ts(wall: str) -> Optional[float]:
    try:
        return time.mktime(time.strptime(wall.strip(), WALL_FMT))
    except (ValueError, OverflowError):
        return None


def fmt_after(secs: Optional[float]) -> str:
    return "missing" if secs is None else f"+{secs:.1f} s"


# --- per-board line reduction -------------------------------------------------


class BoardState:
    """One board's marker stream, host-timestamped.

    Every line becomes zero or one event `(ts, kind, detail, value)`.  All the
    verdict logic lives in `analyse()` and works off that list, so the live
    runner and the offline `report` share one code path.
    """

    def __init__(self, name: str, kind: str = KIND_ESP32) -> None:
        self.name = name
        self.kind = kind
        self.kind_locked = False
        self.events: List[Dict[str, Any]] = []
        self.lines = 0
        self.ready_seen = False
        self.link_up: Optional[bool] = None
        self.has_ip: Optional[bool] = None
        self.eth_rx_n: Optional[int] = None
        self.counts: Dict[str, int] = {}
        self.commands: List[Tuple[float, str]] = []   # (ts, text) sent by us
        self.error: Optional[str] = None

    # -- helpers
    def _ev(self, ts: float, kind: str, detail: str = "", value: str = "",
            **extra: Any) -> Dict[str, Any]:
        row = dict(ts=ts, board=self.name, kind=kind, detail=detail, value=value)
        row.update(extra)
        self.events.append(row)
        self.counts[kind] = self.counts.get(kind, 0) + 1
        return row

    def _kind_from(self, line: str) -> None:
        if self.kind_locked:
            return
        if "[ETH];" in line:
            self.kind, self.kind_locked = KIND_RAK, True
        elif "[WIFI];" in line:
            self.kind, self.kind_locked = KIND_ESP32, True

    def note_command(self, ts: float, text: str) -> None:
        self.commands.append((ts, text))
        self._ev(ts, "command", text)

    # -- the line reducer
    def add(self, ts: float, line: str) -> None:
        self.lines += 1
        self._kind_from(line)

        m = wifisoak.RE_RESET.search(line)
        if m:
            self._ev(ts, "reset", m.group(0))
            return
        m = wifisoak.RE_READY.search(line)
        if m:
            self.ready_seen = True
            self._ev(ts, "boot", f"ip={m.group('ip')}", m.group("ms"))
            return

        # --- ESP32 / WiFi
        m = wifisoak.RE_DISC.search(line)
        if m:
            r = int(m.group("reason"))
            self.link_up = False
            self._ev(ts, "link_down", f"{r}:{wifisoak.REASONS.get(r, '?')}", m.group("ms"))
            return
        m = wifisoak.RE_ASSOC.search(line)
        if m:
            if m.group("what") == "got_ip":
                self.has_ip = True
                self._ev(ts, "got_ip", "assoc", m.group("ms"),
                         bssid=m.group("bssid"), chan=m.group("chan"), rssi=m.group("rssi"))
            return
        m = wifisoak.RE_EVENT.search(line)
        if m:
            if m.group("what") == "got_ip":
                self.has_ip = True
                self._ev(ts, "got_ip", "event", m.group("ms"))
            else:
                self.link_up = True
                self._ev(ts, "link_up", "connected", m.group("ms"))
            return
        m = wifisoak.RE_LINK_UP.search(line)
        if m:
            self.link_up = True
            self._ev(ts, "link_up", "heartbeat", m.group("ms"),
                     bssid=m.group("bssid"), chan=m.group("chan"), rssi=m.group("rssi"))
            if m.group("ip") == "1":
                self.has_ip = True
                self._ev(ts, "got_ip", "heartbeat", m.group("ms"))
            return
        m = wifisoak.RE_LINK_DOWN.search(line)
        if m:
            self.link_up = False
            self._ev(ts, "link_down", "heartbeat", m.group("ms"))
            return
        m = wifisoak.RE_WD.search(line)
        if m:
            self._ev(ts, "watchdog", m.group("what"), m.group("down"))
            return
        m = wifisoak.RE_STALL.search(line)
        if m:
            self._ev(ts, "stall", m.group("site"), m.group("ms"), note=m.group("task"))
            return

        # --- RAK / Ethernet
        m = RE_ETH_EV_LINK.search(line)
        if m:
            up = m.group("what") == "up"
            self.link_up = up
            self._ev(ts, "link_up" if up else "link_down", "eth event", m.group("ms"))
            return
        m = RE_ETH_EV_GOT_IP.search(line)
        if m:
            self.has_ip = True
            self._ev(ts, "got_ip", f"eth {m.group('ip')}", m.group("ms"))
            return
        m = RE_ETH_EV_DHCP.search(line)
        if m:
            self._ev(ts, "dhcp", f"rc={m.group('rc')}", m.group("ms"))
            return
        m = RE_ETH_EV_RESET.search(line)
        if m:
            self._ev(ts, "eth_reset", "", m.group("ms"))
            return
        m = RE_ETH_HB.search(line)
        if m:
            self.link_up = m.group("lnk") == "1"
            if self.link_up:
                self._ev(ts, "link_up", "eth heartbeat", m.group("ms"))
            else:
                self._ev(ts, "link_down", "eth heartbeat", m.group("ms"))
            if m.group("state") == "up" and m.group("ip") != "0.0.0.0":
                self.has_ip = True
                self._ev(ts, "got_ip", f"eth heartbeat {m.group('ip')}", m.group("ms"))
            rx = int(m.group("rx"))
            if self.eth_rx_n is not None and rx > self.eth_rx_n:
                # the RAK has no [UDP];rx line: the heartbeat's rx_n delta is
                # the only witness that server traffic is coming back
                self._ev(ts, "udp_rx", f"eth rx_n +{rx - self.eth_rx_n}", str(rx))
            self.eth_rx_n = rx
            return
        m = RE_ETH_STALL.search(line)
        if m:
            self._ev(ts, "stall", m.group("site"), m.group("ms"), note=m.group("task"))
            return

        # --- shared
        m = RE_UDP_TX.search(line)
        if m:
            self._ev(ts, "udp_tx", f"len={m.group('len')} ok={m.group('ok')}", m.group("ip"))
            return
        m = RE_UDP_RX.search(line)
        if m:
            self._ev(ts, "udp_rx", f"len={m.group('len')} head={m.group('head')}", m.group("ip"))
            return
        m = RE_NTP_OK.search(line)
        if m:
            self._ev(ts, "ntp_ok", f"rtt={m.group('rtt')}", m.group("epoch"))
            return
        m = RE_NTP_FAIL.search(line)
        if m:
            self._ev(ts, "ntp_fail", m.group("what"), m.group("ip"))
            return
        m = RE_UDP_LOG.search(line)
        if m:
            self._ev(ts, "udplog_ack", m.group("on"))
            return

    # -- live one-liner for status.txt
    def brief(self) -> str:
        c = self.counts
        return (f"link={'up' if self.link_up else 'down' if self.link_up is False else '?'} "
                f"ip={'1' if self.has_ip else '0' if self.has_ip is False else '?'} "
                f"tx={c.get('udp_tx', 0)} rx={c.get('udp_rx', 0)} ntp={c.get('ntp_ok', 0)} "
                f"boots={c.get('boot', 0)} rst={c.get('reset', 0)} wd={c.get('watchdog', 0)} "
                f"lines={self.lines}")


# --- verdict ------------------------------------------------------------------


class BoardResult:
    def __init__(self, name: str, kind: str) -> None:
        self.name = name
        self.kind = kind
        self.before: Dict[str, int] = {}
        self.after: Dict[str, Optional[float]] = {}
        self.after_state: Dict[str, str] = {}    # ok | missing | n/a | late
        self.reboots_after = 0
        self.resets = 0
        self.boots = 0
        self.watchdog: List[str] = []
        self.stalls: List[Tuple[str, int]] = []
        self.commands_after: List[str] = []
        self.fails: List[str] = []
        self.notes: List[str] = []

    @property
    def passed(self) -> bool:
        return not self.fails


def analyse_board(st: BoardState, t0: Optional[float],
                  deadline: Optional[float], require_ntp: bool = False) -> BoardResult:
    res = BoardResult(st.name, st.kind)
    res.resets = st.counts.get("reset", 0)
    res.boots = st.counts.get("boot", 0)

    for m in MARKERS:
        res.before[m] = sum(1 for e in st.events
                            if e["kind"] == m and (t0 is None or e["ts"] < t0))

    for e in st.events:
        if e["kind"] == "watchdog":
            res.watchdog.append(f"{e['detail']}(down_s={e['value']})")
        elif e["kind"] == "stall":
            try:
                res.stalls.append((e["detail"], int(e["value"])))
            except ValueError:
                pass

    if t0 is None:
        for m in MARKERS:
            res.after[m] = None
            res.after_state[m] = "-"
        res.fails.append("no outage detected")
        return res

    unsupported = UNSUPPORTED.get(st.kind, ())
    ntp_tried_after = any(e["kind"] in ("ntp_ok", "ntp_fail") and e["ts"] >= t0
                          for e in st.events)
    for m in MARKERS:
        hits = [e["ts"] for e in st.events if e["kind"] == m and e["ts"] >= t0]
        first = min(hits) - t0 if hits else None
        res.after[m] = first
        if m in unsupported:
            res.after_state[m] = "n/a (no marker on this platform)"
            continue
        if m == "ntp_ok":
            # a refresh is only due every 15 min (esp32_main.cpp:2628,
            # nrf52_main.cpp:1214) and never at all while the node has a GPS
            # fix, so require an [NTP];ok only when a refresh actually fired
            # after t0 -- or when --require-ntp says the window is long enough
            required = ntp_tried_after or require_ntp
        else:
            required = m in ALWAYS_REQUIRED or res.before[m] > 0
        if first is None:
            if required:
                res.after_state[m] = "missing"
                res.fails.append(
                    "ntp_ok missing (an NTP refresh after t0 failed)"
                    if m == "ntp_ok" and ntp_tried_after else f"{m} missing")
            elif m == "ntp_ok":
                res.after_state[m] = ("n/a (no refresh due in the window)"
                                      if res.before[m] else "n/a (no NTP before t0)")
            else:
                res.after_state[m] = "n/a (none before t0)"
        elif deadline is not None and t0 + first > deadline:
            res.after_state[m] = "late"
            if required:
                res.fails.append(f"{m} only after the recovery window")
        else:
            res.after_state[m] = "ok"

    res.reboots_after = sum(1 for e in st.events
                            if e["kind"] in ("reset", "boot") and e["ts"] >= t0)
    if res.reboots_after:
        res.fails.append(f"rebooted after t0 ({res.reboots_after} reset/boot marker(s))")
    if res.resets + res.boots > 2:
        res.notes.append(f"{res.resets} rst:0x and {res.boots} [BOOT];ready over the whole run")

    res.commands_after = [f"{c[1]} at t0+{c[0] - t0:.0f} s"
                          for c in st.commands if c[0] >= t0]
    if res.commands_after:
        res.fails.append("a serial command was sent after t0: "
                         + ", ".join(res.commands_after))
    return res


def analyse_run(states: Sequence[BoardState], meta: Dict[str, Any]) -> Dict[str, Any]:
    """Verdict for the whole run.  `meta` may be empty (bare-log report)."""
    # Only edges after the prompt count: every ESP32 board prints a
    # [WIFI];link;down while it is still booting, and a run that never reached
    # PROMPT (stopped, settle gate) has no outage at all -- taking those as t0
    # would invent a verdict.  `prompt_ts` absent entirely means bare logs with
    # no meta.json, where the first down edge is the best guess available.
    prompt_ts = meta.get("prompt_ts")
    never_prompted = "prompt_ts" in meta and prompt_ts is None
    down = [] if never_prompted else [
        (e["ts"], s.name, e["detail"]) for s in states for e in s.events
        if e["kind"] == "link_down" and (prompt_ts is None or e["ts"] >= prompt_ts)]
    down.sort()
    t0 = down[0][0] if down else None
    recovery = meta.get("recovery")
    deadline = (t0 + recovery) if (t0 is not None and recovery) else None

    results = [analyse_board(s, t0, deadline, bool(meta.get("require_ntp")))
               for s in states]
    fails: List[str] = []
    if t0 is None:
        fails.append("no outage detected")
    for r in results:
        for f in r.fails:
            if f != "no outage detected":
                fails.append(f"{r.name}: {f}")
    for s in states:
        if s.error:
            fails.append(f"{s.name}: serial error: {s.error}")
    for f in meta.get("extra_fails", []):
        fails.append(f)

    return dict(t0=t0, t0_board=down[0][1] if down else None,
                t0_detail=down[0][2] if down else None,
                deadline=deadline, results=results,
                verdict="PASS" if not fails else "FAIL", fails=fails)


# --- report writers -----------------------------------------------------------

def write_events_csv(path: Path, states: Sequence[BoardState], t0: Optional[float]) -> None:
    rows = sorted((e for s in states for e in s.events), key=lambda e: e["ts"])
    with open(path, "w", newline="", encoding="utf-8") as fh:
        w = csv.DictWriter(fh, fieldnames=wifisoak.EVENT_FIELDS)
        w.writeheader()
        for e in rows:
            note = e.get("note", "")
            if t0 is not None:
                note = (note + " " if note else "") + f"t0{e['ts'] - t0:+.1f}s"
            w.writerow(dict(board=e["board"], wall=wall_now(e["ts"]),
                            uptime_ms=e.get("value", "") if e["kind"] in
                            ("boot",) else "",
                            kind=e["kind"], reason=e.get("detail", ""),
                            bssid=e.get("bssid", ""), chan=e.get("chan", ""),
                            rssi=e.get("rssi", ""), value=e.get("value", ""),
                            note=note))


def render_summary(states: Sequence[BoardState], meta: Dict[str, Any],
                   run: Dict[str, Any]) -> str:
    out: List[str] = []
    out.append(f"apreboot {meta.get('label') or '(no label)'}  {meta.get('rundir', '')}")
    if meta.get("started"):
        out.append(f"started            {wall_now(meta['started'])}")
    out.append("windows            settle {} s / cycle {} s / recovery {} s".format(
        meta.get("settle", "?"), meta.get("cycle_window", "?"), meta.get("recovery", "?")))
    if run["t0"] is not None:
        out.append(f"outage t0          {wall_now(run['t0'])}  "
                   f"(first down edge: {run['t0_board']} {run['t0_detail']})")
    else:
        out.append("outage t0          none seen")
    out.append(f"verdict            {run['verdict']}")
    for f in run["fails"]:
        out.append(f"  !! {f}")
    for w in meta.get("warnings", []):
        out.append(f"  WARN {w}")
    out.append("")

    for res in run["results"]:
        out.append(f"== {res.name} ({res.kind})   {'PASS' if res.passed else 'FAIL'}")
        out.append(f"{'marker':<12}{'before t0':>10}   after t0")
        for m in MARKERS:
            state = res.after_state.get(m, "-")
            when = fmt_after(res.after[m]) if res.after.get(m) is not None else ""
            if state == "ok":
                shown = when
            elif state == "late":
                shown = f"{when}  LATE (outside the recovery window)"
            else:
                shown = state
            out.append(f"{m:<12}{res.before.get(m, 0):>10}   {shown}")
        out.append(f"{'reboots after t0':<24}{res.reboots_after}")
        out.append(f"{'rst:0x / [BOOT];ready':<24}{res.resets} / {res.boots}")
        out.append(f"{'[WIFI];watchdog':<24}{len(res.watchdog)}"
                   + (f"  {res.watchdog}" if res.watchdog else ""))
        if res.stalls:
            worst = max(res.stalls, key=lambda s: s[1])
            over = sum(1 for s in res.stalls if s[1] > 500)
            out.append(f"{'stall lines':<24}{len(res.stalls)}  max {worst[1]} ms "
                       f"at {worst[0]}  >500 ms: {over}")
        else:
            out.append(f"{'stall lines':<24}0")
        out.append(f"{'commands after t0':<24}"
                   + (", ".join(res.commands_after) if res.commands_after else "none"))
        for n in res.notes:
            out.append(f"  note: {n}")
        for f in res.fails:
            out.append(f"  !! {f}")
        out.append("")
    return "\n".join(out)


# --- serial plumbing ----------------------------------------------------------


def real_opener(port: str, kind: str) -> Any:  # pragma: no cover - needs hardware
    if serial is None:
        raise RuntimeError("pyserial is required to run (report works without it)")
    s = serial.Serial()
    s.port = port
    s.baudrate = BAUD
    s.timeout = 0.2
    # ESP32 bench nodes reset on open unless both lines stay low -- and they
    # still do, which is fine (that is the run's first, expected boot).  The
    # RAK4631 does not reset on open but stays silent without DTR.
    s.dtr = (kind == KIND_RAK)
    s.rts = False
    s.open()
    return s


class BoardReader(threading.Thread):
    """Held-open USB session, tolerating a port that vanishes and comes back."""

    def __init__(self, name: str, port: str, kind: str, outdir: Path,
                 opener: Callable[[str, str], Any]) -> None:
        super().__init__(daemon=True, name=f"reader-{name}")
        self.name_ = name
        self.port = port
        self.kind = kind
        self.opener = opener
        self.state = BoardState(name, kind)
        self.stop = threading.Event()
        self.lock = threading.Lock()
        self.log = open(outdir / f"{name}.log", "a", buffering=1, encoding="utf-8")
        self.ser: Any = None
        self.open_ts: Optional[float] = None
        self.reopens = 0
        self.udplog_sent: Optional[float] = None

    def _note(self, text: str) -> None:
        self.log.write(f"{wall_now()} ## {text}\n")

    def run(self) -> None:
        buf = b""
        while not self.stop.is_set():
            if self.ser is None:
                try:
                    self.ser = self.opener(self.port, self.kind)
                    self.open_ts = time.time()
                    self.reopens += 1
                    with self.lock:
                        self.state.error = None
                    self._note(f"opened {self.port} (#{self.reopens})")
                except Exception as e:  # noqa: BLE001
                    with self.lock:
                        self.state.error = str(e)
                    self._note(f"open failed: {e}")
                    self.stop.wait(1.0)
                    continue
            try:
                chunk = self.ser.read(4096)
            except Exception as e:  # noqa: BLE001
                self._note(f"read failed, will reopen: {e}")
                self._close()
                self.stop.wait(1.0)
                continue
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                line = raw.decode("utf-8", errors="replace").rstrip("\r")
                ts = time.time()
                self.log.write(f"{wall_now(ts)} {line}\n")
                with self.lock:
                    self.state.add(ts, line)
        self._close()

    def _close(self) -> None:
        if self.ser is not None:
            try:
                self.ser.close()
            except Exception:  # noqa: BLE001
                pass
        self.ser = None

    def send(self, cmd: str) -> bool:
        """The firmware consumes one char per loop(): pace the characters."""
        if self.ser is None:
            return False
        try:
            for ch in cmd:
                self.ser.write(ch.encode())
                self.ser.flush()
                time.sleep(0.02)
            self.ser.write(b"\n")
            self.ser.flush()
        except Exception as e:  # noqa: BLE001
            self._note(f"send failed: {e}")
            return False
        ts = time.time()
        self._note(f">> {cmd}")
        with self.lock:
            self.state.note_command(ts, cmd)
        self.udplog_sent = ts
        return True


# --- operator notification ----------------------------------------------------


def notify(text: str, spoken: str) -> None:
    """macOS notification + spoken prompt, both best effort.

    The runner has no controlling terminal by then, so this is the only way it
    can reach the operator; failures are ignored on purpose.
    """
    def _run() -> None:
        for argv in (["osascript", "-e",
                      f'display notification "{text}" with title "MeshCom bench: apreboot"'],
                     ["say", spoken]):
            try:
                subprocess.run(argv, timeout=20, check=False,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            except Exception:  # noqa: BLE001
                pass
    threading.Thread(target=_run, daemon=True).start()


# --- the phase machine --------------------------------------------------------


def run_session(rundir: Path, boards: Sequence[Tuple[str, str, str]], settle: float,
                cycle_window: float, recovery: float, label: str,
                opener: Callable[[str, str], Any] = real_opener,
                status_every: float = 10.0, udplog_after: float = 30.0,
                do_notify: bool = True, require_ntp: bool = False,
                strict_udp: bool = False) -> int:
    readers = [BoardReader(n, p, k, rundir, opener) for n, p, k in boards]
    for r in readers:
        r.start()

    t_start = time.time()
    meta: Dict[str, Any] = dict(label=label, rundir=str(rundir), started=t_start,
                                settle=settle, cycle_window=cycle_window,
                                recovery=recovery, pid=os.getpid(),
                                require_ntp=require_ntp, prompt_ts=None,
                                boards=[dict(name=n, port=p, kind=k) for n, p, k in boards])

    def save_meta(extra: Optional[Dict[str, Any]] = None) -> None:
        m = dict(meta)
        m.update(extra or {})
        (rundir / "meta.json").write_text(json.dumps(m, indent=2) + "\n", encoding="utf-8")

    save_meta()

    stop_evt = threading.Event()
    for sig in (signal.SIGINT, signal.SIGTERM):
        try:
            signal.signal(sig, lambda *_: stop_evt.set())
        except ValueError:  # pragma: no cover - not the main thread
            pass

    phase = "SETTLE"
    t_prompt: Optional[float] = None
    t0: Optional[float] = None
    extra_fails: List[str] = []
    warnings: List[str] = []
    next_status = 0.0

    def states() -> List[BoardState]:
        out = []
        for r in readers:
            with r.lock:
                out.append(r.state)
        return out

    def snapshot_meta() -> Dict[str, Any]:
        m = dict(meta)
        m["prompt_ts"] = t_prompt
        m["extra_fails"] = list(extra_fails)
        m["warnings"] = list(warnings)
        return m

    def write_status(note: str = "") -> None:
        now = time.time()
        lines = [f"apreboot {label or '(no label)'}  {rundir}",
                 f"now                {wall_now(now)}",
                 f"phase              {phase}" + (f"   {note}" if note else "")]
        if phase == "SETTLE":
            lines.append(f"settle             t+{now - t_start:.0f} s of {settle:.0f}")
        elif phase == "PROMPT" and t_prompt is not None:
            lines.append("*** CYCLE THE ACCESS POINTS NOW ***")
            lines.append(f"cycle window       t+{now - t_prompt:.0f} s of {cycle_window:.0f}")
        elif phase == "RECOVERY" and t0 is not None:
            lines.append(f"outage t0          {wall_now(t0)}")
            lines.append(f"recovery           t0+{now - t0:.0f} s of {recovery:.0f}")
        for w in warnings:
            lines.append(f"WARN               {w}")
        lines.append("")
        lines.append(f"{'board':<8}{'kind':<7}state")
        for r in readers:
            with r.lock:
                st = r.state
                lines.append(f"{st.name:<8}{st.kind:<7}{st.brief()}"
                             + (f"  !! {st.error}" if st.error else ""))
        text = "\n".join(lines) + "\n"
        (rundir / "status.txt").write_text(text, encoding="utf-8")
        write_events_csv(rundir / "events.csv", states(), t0)

    def finish(reason: str) -> int:
        for r in readers:
            r.stop.set()
        for r in readers:
            r.join(timeout=3)
        sts = states()
        m = snapshot_meta()
        run = analyse_run(sts, m)
        write_events_csv(rundir / "events.csv", sts, run["t0"])
        text = render_summary(sts, m, run)
        (rundir / "summary.txt").write_text(text + "\n", encoding="utf-8")
        (rundir / "status.txt").write_text(
            f"apreboot {label or '(no label)'}  {rundir}\n"
            f"phase              DONE ({reason})\n"
            f"verdict            {run['verdict']}\n"
            f"see                {rundir / 'summary.txt'}\n", encoding="utf-8")
        save_meta(dict(prompt_ts=t_prompt, warnings=list(warnings),
                       extra_fails=list(extra_fails), t0=run["t0"],
                       verdict=run["verdict"]))
        print(text, flush=True)
        return 0 if run["verdict"] == "PASS" else 1

    while True:
        now = time.time()
        if stop_evt.is_set():
            extra_fails.append("run stopped before the recovery window ended")
            return finish("stopped")

        if phase == "SETTLE":
            for r in readers:
                # bUDPLOG lives in the ESP32 half of udp_functions.cpp; the RAK
                # answers "...wrong command" and would gain nothing anyway
                if r.kind == KIND_RAK:
                    continue
                if r.udplog_sent is None and r.ser is not None and r.open_ts is not None:
                    with r.lock:
                        ready = r.state.ready_seen
                    if ready or now - r.open_ts >= udplog_after:
                        r.send("--udplog on")
            if now - t_start >= settle:
                missing = []
                for r in readers:
                    with r.lock:
                        st = r.state
                        if st.error:
                            missing.append(f"{st.name}: serial error: {st.error}")
                            continue
                        if not any(e["kind"] == "link_up" for e in st.events):
                            missing.append(f"{st.name}: no link up during settle")
                        if not any(e["kind"] == "got_ip" for e in st.events):
                            missing.append(f"{st.name}: no IP during settle")
                        if (st.kind != KIND_RAK
                                and not any(e["kind"] == "udp_tx" for e in st.events)):
                            note = (f"{st.name}: no [UDP];tx during settle -- the node is "
                                    f"not a gateway (KEEP is sent only with --gateway on), "
                                    f"so the UDP half of the test is not exercised")
                            (missing if strict_udp else warnings).append(note)
                if missing:
                    extra_fails.extend(f"settle incomplete -- {x}" for x in missing)
                    return finish("settle incomplete")
                phase = "PROMPT"
                t_prompt = now
                save_meta(dict(prompt_ts=t_prompt, warnings=list(warnings)))
                (rundir / "PROMPT").write_text(
                    f"{wall_now(now)}  CYCLE THE ACCESS POINTS NOW "
                    f"(within {cycle_window:.0f} s)\n", encoding="utf-8")
                print(f"{wall_now(now)} *** CYCLE THE ACCESS POINTS NOW *** "
                      f"(window {cycle_window:.0f} s)", flush=True)
                if do_notify:
                    notify("Cycle the access points now.",
                           "cycle the access points now")
                write_status()

        elif phase == "PROMPT":
            edges = []
            for r in readers:
                with r.lock:
                    edges.extend((e["ts"], r.state.name, e["detail"])
                                 for e in r.state.events
                                 if e["kind"] == "link_down" and t_prompt is not None
                                 and e["ts"] >= t_prompt)
            if edges:
                edges.sort()
                t0 = edges[0][0]
                phase = "RECOVERY"
                print(f"{wall_now(t0)} outage detected on {edges[0][1]} "
                      f"({edges[0][2]}) -- recovery window {recovery:.0f} s", flush=True)
                write_status()
            elif t_prompt is not None and now - t_prompt >= cycle_window:
                return finish("no outage detected")

        elif phase == "RECOVERY":
            if t0 is not None and now - t0 >= recovery:
                return finish("recovery window elapsed")

        if now >= next_status:
            write_status()
            next_status = now + status_every
        time.sleep(0.25)


# --- detaching ----------------------------------------------------------------


def detach(rundir: Path) -> None:
    """Double fork + setsid, stdio to files, pid file.

    The point of the whole exercise: the launching shell (and the WiFi it may
    be reached over) goes away when the APs reboot, and the run has to survive
    it.  Called only in the child; the parent returns from `spawn()`.
    """
    os.setsid()
    if os.fork() > 0:
        os._exit(0)
    os.chdir("/")
    sys.stdout.flush()
    sys.stderr.flush()
    devnull = os.open(os.devnull, os.O_RDWR)
    os.dup2(devnull, 0)
    out = os.open(str(rundir / "runner.out"), os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o644)
    err = os.open(str(rundir / "runner.err"), os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o644)
    os.dup2(out, 1)
    os.dup2(err, 2)
    (rundir / "runner.pid").write_text(f"{os.getpid()}\n", encoding="utf-8")


def pid_alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


# --- run directory helpers ----------------------------------------------------


def default_runs_dir() -> Path:
    return Path(__file__).resolve().parent.parent / "runs"


def newest_run(runs_dir: Path) -> Optional[Path]:
    cands = sorted(p for p in runs_dir.glob(f"{RUN_PREFIX}_*") if p.is_dir())
    return cands[-1] if cands else None


def resolve_rundir(arg: Optional[str], runs_dir: Path) -> Path:
    if arg:
        p = Path(arg)
        if p.is_dir():
            return p
        hits = sorted(Path(x) for x in glob.glob(arg))
        if hits:
            return hits[-1]
        raise SystemExit(f"no such run directory: {arg}")
    p = newest_run(runs_dir)
    if p is None:
        raise SystemExit(f"no {RUN_PREFIX}_* run under {runs_dir}")
    return p


def parse_board_spec(spec: str) -> Tuple[str, str, str]:
    """`name=/dev/port` or `name=/dev/port:kind` (kind: esp32 | rak).

    Without an explicit kind, a board whose name contains "rak" gets the RAK
    profile (DTR high, [ETH] markers), everything else the ESP32 one.
    """
    if "=" not in spec:
        raise SystemExit(f"--board wants name=/dev/port, got {spec!r}")
    name, port = spec.split("=", 1)
    kind = ""
    if ":" in port:
        port, maybe = port.rsplit(":", 1)
        if maybe in (KIND_ESP32, KIND_RAK):
            kind = maybe
        else:
            port = f"{port}:{maybe}"
    if not kind:
        kind = KIND_RAK if "rak" in name.lower() else KIND_ESP32
    return name, port, kind


# --- offline report -----------------------------------------------------------


def states_from_logs(paths: Sequence[Path]) -> List[BoardState]:
    out = []
    for p in paths:
        name = p.name.rsplit(".", 1)[0]
        st = BoardState(name, KIND_RAK if "rak" in name.lower() else KIND_ESP32)
        base = None
        with open(p, encoding="utf-8", errors="replace") as fh:
            for line in fh:
                line = line.rstrip("\n")
                ts = wall_to_ts(line[:19]) if len(line) > 19 else None
                rest = line[20:] if ts is not None else line
                if ts is None:
                    ts = base if base is not None else 0.0
                base = ts
                if rest.startswith("## >> "):
                    st.note_command(ts, rest[6:].strip())
                    continue
                if rest.startswith("## "):
                    continue
                st.add(ts, rest)
        out.append(st)
    return out


def cmd_report(args: argparse.Namespace) -> int:
    runs_dir = Path(args.runs_dir) if args.runs_dir else default_runs_dir()
    targets = list(args.target)
    rundir: Optional[Path] = None
    logs: List[Path] = []
    if not targets:
        rundir = resolve_rundir(None, runs_dir)
    else:
        for t in targets:
            hits = sorted(Path(x) for x in glob.glob(t)) or [Path(t)]
            for h in hits:
                if h.is_dir():
                    rundir = h
                elif h.exists():
                    logs.append(h)
                else:
                    raise SystemExit(f"no such path: {t}")
    meta: Dict[str, Any] = {}
    if rundir is not None:
        mp = rundir / "meta.json"
        if mp.exists():
            meta = json.loads(mp.read_text(encoding="utf-8"))
        logs.extend(sorted(p for p in rundir.glob("*.log")
                           if p.name not in ("runner.out", "runner.err")))
    logs = [p for p in logs if p.exists()]
    if not logs:
        raise SystemExit("no board logs to report on")

    states = states_from_logs(logs)
    if meta.get("boards"):
        by_name = {b["name"]: b.get("kind", KIND_ESP32) for b in meta["boards"]}
        for st in states:
            if not st.kind_locked and st.name in by_name:
                st.kind = by_name[st.name]
    run = analyse_run(states, meta)
    text = render_summary(states, meta or dict(rundir=str(rundir or "")), run)
    print(text)
    if rundir is not None and not args.no_write:
        write_events_csv(rundir / "events.csv", states, run["t0"])
        (rundir / "summary.txt").write_text(text + "\n", encoding="utf-8")
        print(f"{rundir / 'summary.txt'}")
    return 0 if run["verdict"] == "PASS" else 1


# --- commands -----------------------------------------------------------------


def cmd_start(args: argparse.Namespace) -> int:
    runs_dir = Path(args.runs_dir) if args.runs_dir else default_runs_dir()
    runs_dir.mkdir(parents=True, exist_ok=True)
    boards = [parse_board_spec(s) for s in args.board]
    if not boards:
        raise SystemExit("at least one --board name=/dev/port")
    label = f"{args.label}_" if args.label else ""
    rundir = runs_dir / f"{RUN_PREFIX}_{label}{time.strftime('%Y%m%d-%H%M%S')}"
    rundir.mkdir(parents=True, exist_ok=True)

    rundir = rundir.resolve()
    kwargs = dict(settle=args.settle, cycle_window=args.cycle_window,
                  recovery=args.recovery, label=args.label,
                  do_notify=not args.no_notify, require_ntp=args.require_ntp,
                  strict_udp=args.strict_udp)
    total = args.settle + args.cycle_window + args.recovery

    if args.foreground:
        return run_session(rundir, boards, **kwargs)  # type: ignore[arg-type]

    pid = os.fork()
    if pid > 0:
        os.waitpid(pid, 0)          # the intermediate child exits at once
        pidfile = rundir / "runner.pid"
        for _ in range(100):
            if pidfile.exists():
                break
            time.sleep(0.05)
        runner_pid = pidfile.read_text().strip() if pidfile.exists() else "?"
        print(f"apreboot detached, pid {runner_pid}")
        print(f"  run dir   {rundir}")
        print(f"  phases    settle {args.settle:.0f} s -> prompt (window "
              f"{args.cycle_window:.0f} s) -> recovery {args.recovery:.0f} s "
              f"(~{total / 60:.0f} min total)")
        print(f"  watch     python3 {Path(__file__).name} status")
        print(f"  stop      python3 {Path(__file__).name} stop")
        print("  the runner survives this shell, the WLAN and the Mac losing "
              "the network; it uses USB serial only.")
        return 0

    # child -> grandchild
    detach(rundir)
    code = 1
    try:
        code = run_session(rundir, boards, **kwargs)  # type: ignore[arg-type]
    except BaseException as e:  # noqa: BLE001
        try:
            (rundir / "runner.err").open("a").write(f"{wall_now()} runner died: {e!r}\n")
        except Exception:  # noqa: BLE001
            pass
    finally:
        sys.stdout.flush()
        sys.stderr.flush()
        os._exit(code)


def cmd_status(args: argparse.Namespace) -> int:
    runs_dir = Path(args.runs_dir) if args.runs_dir else default_runs_dir()
    rundir = resolve_rundir(args.rundir, runs_dir)
    sp = rundir / "status.txt"
    if not sp.exists():
        print(f"{rundir}: no status.txt yet")
        return 1
    print(sp.read_text(encoding="utf-8").rstrip())
    pp = rundir / "runner.pid"
    if pp.exists():
        pid = int(pp.read_text().strip() or 0)
        print(f"\nrunner pid {pid}: {'alive' if pid_alive(pid) else 'gone'}")
    return 0


def cmd_stop(args: argparse.Namespace) -> int:
    runs_dir = Path(args.runs_dir) if args.runs_dir else default_runs_dir()
    rundir = resolve_rundir(args.rundir, runs_dir)
    pp = rundir / "runner.pid"
    if not pp.exists():
        print(f"{rundir}: no runner.pid")
        return 1
    pid = int(pp.read_text().strip() or 0)
    if not pid_alive(pid):
        print(f"{rundir}: pid {pid} already gone")
        return 0
    os.kill(pid, signal.SIGTERM)
    for _ in range(120):
        if not pid_alive(pid):
            print(f"{rundir}: pid {pid} stopped")
            return 0
        time.sleep(0.25)
    os.kill(pid, signal.SIGKILL)
    print(f"{rundir}: pid {pid} killed (did not stop on SIGTERM)")
    return 0


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--runs-dir", default="", help=f"default: {default_runs_dir()}")
    sub = ap.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("start", help="arm a run and detach")
    s.add_argument("--board", action="append", default=[],
                   metavar="NAME=/dev/PORT[:esp32|:rak]", help="repeatable")
    s.add_argument("--settle", type=float, default=180.0)
    s.add_argument("--cycle-window", type=float, default=180.0)
    s.add_argument("--recovery", type=float, default=600.0)
    s.add_argument("--label", default="")
    s.add_argument("--foreground", action="store_true",
                   help="do not detach (debugging only -- dies with the shell)")
    s.add_argument("--no-notify", action="store_true",
                   help="skip the macOS notification and the spoken prompt")
    s.add_argument("--strict-udp", action="store_true",
                   help="abort at the settle gate when an ESP32 board sent no [UDP];tx "
                        "(it is not a gateway, so the UDP half of the test is dead weight)")
    s.add_argument("--require-ntp", action="store_true",
                   help="fail a board without a fresh [NTP];ok after t0 even when no "
                        "refresh became due; only meaningful with --recovery > 900")
    s.set_defaults(func=cmd_start)

    s = sub.add_parser("status", help="print status.txt of a run (default: newest)")
    s.add_argument("rundir", nargs="?")
    s.set_defaults(func=cmd_status)

    s = sub.add_parser("stop", help="SIGTERM the detached runner")
    s.add_argument("rundir", nargs="?")
    s.set_defaults(func=cmd_stop)

    s = sub.add_parser("report", help="parse logs, rebuild events.csv + summary.txt")
    s.add_argument("target", nargs="*", help="run dir (default: newest) or *.log files")
    s.add_argument("--no-write", action="store_true", help="print only")
    s.set_defaults(func=cmd_report)
    return ap


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = build_parser().parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
