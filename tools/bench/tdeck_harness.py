#!/usr/bin/env python3
"""T-Deck Plus host-side automated regression/measurement harness.

Opens the T-Deck Plus USB-serial console ONCE (opening the port resets the
ESP32-S3 -- this is unavoidable, so the harness waits for the "CLIENT
STARTED" boot marker after opening) and keeps that one session for the whole
run, driving a series of scenarios over it via the debug-console commands
and parsing the [REDRAW]/[REFR]/[UISTAT]/... instrumentation lines defined in
tdeck_parse.py.

Usage:
    python3 tools/bench/tdeck_harness.py --scenario all
    python3 tools/bench/tdeck_harness.py --scenario idle --idle-seconds 30
    python3 tools/bench/tdeck_harness.py --help

See serial_session.py / soak_harness.py in this directory for the sibling
bench tools' style; this one is self-contained (no import from either).
"""

from __future__ import annotations

import argparse
import math
import glob
import json
import re
import subprocess
import sys
import threading
import time
from pathlib import Path
from typing import Any, Callable, Dict, Iterable, List, Optional, Sequence, Tuple

try:
    import serial  # pyserial
except ImportError:  # pragma: no cover - exercised only when pyserial is missing
    serial = None  # type: ignore[assignment]

from tdeck_parse import (
    disptest_expected,
    heap_delta,
    parse_line,
    redraw_summary,
    refr_summary,
)

DEFAULT_PORT = "/dev/cu.usbmodem1101"
DEFAULT_BAUD = 115200
REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_ELF = str(REPO_ROOT / ".pio/build/t_deck_plus/firmware.elf")
BOOT_MARKER = "CLIENT STARTED"
READY_MARKER = "[BOOT];ready"
# TM-42: group the central server filters, so injected test traffic never
# reaches the map/dashboard. See docs/automation-runner-runbook.md §2.6.
TEST_GROUP = "TEST"


# --------------------------------------------------------------------------
# Serial session
# --------------------------------------------------------------------------


class TDeckSession:
    """One held-open serial session against the T-Deck Plus.

    Opens the port exactly once (per the hardware's hard constraint that
    opening it resets the device), starts a background reader thread that
    appends every received line as (t_wall, t_mono, line) to an in-memory
    list and to a timestamped raw log file, and exposes send()/wait_for()/
    collect() for scenario code to drive the console over that one session.
    """

    def __init__(
        self,
        port: str = DEFAULT_PORT,
        baud: int = DEFAULT_BAUD,
        boot_timeout: float = 40.0,
        log_path: Optional[Path] = None,
        ready_timeout: float = 60.0,
    ) -> None:
        self.port = port
        self.baud = baud
        self.boot_timeout = boot_timeout
        self.ready_timeout = ready_timeout
        self.boot_ready_ms: Optional[int] = None
        # the "is the console alive" probe after CLIENT STARTED; boards without
        # the T-Deck GUI answer --oledstat instead (see oled_harness.py)
        self.probe_cmd = "--uistat"
        self.probe_pattern = r"\[UISTAT\]"
        self.wake_cmd: Optional[str] = "--tft on"
        self.log_path = log_path or Path(
            f"tdeck_run_{time.strftime('%Y%m%d-%H%M%S')}.log"
        )
        self.lines: List[Tuple[float, float, str]] = []
        self._lock = threading.Lock()
        self._log_lock = threading.Lock()
        self._stop = threading.Event()
        self._paused = threading.Event()
        self._reader_thread: Optional[threading.Thread] = None
        self.ser: Optional["serial.Serial"] = None
        self._logf = open(self.log_path, "a", buffering=1, encoding="utf-8")

    # -- lifecycle ---------------------------------------------------------

    def open(self) -> None:
        if serial is None:
            raise RuntimeError("pyserial is required to open a T-Deck session")
        ser = serial.Serial()
        ser.port = self.port
        ser.baudrate = self.baud
        ser.timeout = 0.2
        ser.dtr = False
        ser.rts = False
        ser.open()
        self.ser = ser
        self._reader_thread = threading.Thread(target=self._reader_loop, daemon=True)
        self._reader_thread.start()
        if self.wait_for(re.escape(BOOT_MARKER), self.boot_timeout, since=0) is None:
            raise TimeoutError(
                f"boot marker {BOOT_MARKER!r} not seen within {self.boot_timeout}s"
            )
        # The T-Deck ignores serial for ~10 s after CLIENT STARTED while the GUI
        # and SD come up. Poll --uistat until the firmware answers.
        deadline = time.monotonic() + 60.0
        answered = False
        while time.monotonic() < deadline:
            idx = self.send(self.probe_cmd)
            if self.wait_for(self.probe_pattern, 2.0, since=idx) is not None:
                answered = True
                break
        if not answered:
            raise TimeoutError(f"device never answered {self.probe_cmd} within 60s after boot")
        # Answering --uistat is not the end of initialisation: the WiFi attempt
        # (up to ~15 s, plus one radio reset and retry) still runs, the header
        # icons blink and commands are serviced late. The firmware prints
        # [BOOT];ready;ms;<millis> once bAllStarted is true; wait for it, but
        # tolerate its absence (older firmware) after ready_timeout.
        # printfdeb() drops the semicolons outside --debug csv; accept both
        m = self.wait_for(r"\[BOOT\][; ]ready", self.ready_timeout, since=0)
        self.boot_ready_ms = None
        if m:
            mm = re.search(r"ready[; ]ms[; ](\d+)", m.string)
            self.boot_ready_ms = int(mm.group(1)) if mm else None
        if m is None:
            self._write_raw_log(
                f"## no {READY_MARKER} within {self.ready_timeout}s -- continuing anyway"
            )
        # The panel goes dark TDECK_TFT_TIMEOUT (30 s) after the last key or
        # touch, serial traffic does not count; switch it on for the eye tests.
        if self.wake_cmd:
            self.send(self.wake_cmd)
        time.sleep(1.0)

    def close(self) -> None:
        self._stop.set()
        if self._reader_thread is not None:
            self._reader_thread.join(timeout=2.0)
        if self.ser is not None:
            try:
                self.ser.close()
            except Exception:
                pass
        self._logf.close()

    def __enter__(self) -> "TDeckSession":
        self.open()
        return self

    def __exit__(self, *exc: object) -> None:
        self.close()

    # -- reader thread -------------------------------------------------------

    def pause_reader(self, paused: bool) -> None:
        """Stop (or resume) draining the port. While paused the host side
        stops issuing USB IN transfers once its driver buffer is full, which
        is what an unplugged cable or a closed terminal looks like to the
        node's HWCDC TX path (CDC-01)."""
        if paused:
            self._paused.set()
        else:
            self._paused.clear()

    def _reader_loop(self) -> None:
        assert self.ser is not None
        while not self._stop.is_set():
            if self._paused.is_set():
                time.sleep(0.05)
                continue
            try:
                raw = self.ser.readline()
            except Exception:
                break
            if not raw:
                continue
            line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
            if not line:
                continue
            t_wall = time.time()
            t_mono = time.monotonic()
            with self._lock:
                self.lines.append((t_wall, t_mono, line))
            self._write_raw_log(line, t_wall=t_wall)

    @staticmethod
    def _timestamp(t_wall: Optional[float] = None) -> str:
        t = t_wall if t_wall is not None else time.time()
        return time.strftime("%H:%M:%S", time.localtime(t)) + f".{int((t % 1) * 1000):03d}"

    def _write_raw_log(self, text: str, t_wall: Optional[float] = None) -> None:
        with self._log_lock:
            self._logf.write(f"{self._timestamp(t_wall)} {text}\n")

    # -- driving the console -------------------------------------------------

    def send(self, cmd: str) -> int:
        """Send a command, terminated with \\r\\n, and log it with a '>>' prefix.

        Returns the index into self.lines at the moment of sending, so the
        caller can pass it as `since=` to wait_for()/records_since() and be
        certain to see every line produced in response.
        """
        assert self.ser is not None, "session not open"
        with self._lock:
            idx = len(self.lines)
        self._write_raw_log(f">> {cmd}")
        self.ser.write((cmd + "\r\n").encode("utf-8"))
        self.ser.flush()
        return idx

    def length(self) -> int:
        with self._lock:
            return len(self.lines)

    def records_since(self, idx: int) -> List[Tuple[float, float, str]]:
        with self._lock:
            return list(self.lines[idx:])

    def records_range(self, start: int, end: int) -> List[Tuple[float, float, str]]:
        with self._lock:
            return list(self.lines[start:end])

    def wait_for(
        self, pattern: str, timeout: float, since: Optional[int] = None
    ) -> Optional["re.Match[str]"]:
        """Block until a line matching `pattern` (searched, not anchored) arrives.

        Only lines received at or after index `since` (default: the length of
        self.lines at the moment wait_for is called) are considered. Returns
        the match, or None on timeout.
        """
        regex = re.compile(pattern)
        idx = since if since is not None else self.length()
        deadline = time.monotonic() + timeout
        while True:
            with self._lock:
                n = len(self.lines)
                for i in range(idx, n):
                    m = regex.search(self.lines[i][2])
                    if m:
                        return m
                idx = n
            if time.monotonic() >= deadline:
                return None
            time.sleep(0.02)

    def collect(self, seconds: float, since: Optional[int] = None) -> List[str]:
        """Block for `seconds`, then return every line received in that window."""
        idx = since if since is not None else self.length()
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            time.sleep(0.02)
        with self._lock:
            return [self.lines[i][2] for i in range(idx, len(self.lines))]


# --------------------------------------------------------------------------
# Symbolization
# --------------------------------------------------------------------------


def find_addr2line() -> Optional[str]:
    """Locate xtensa-esp32s3-elf-addr2line under ~/.platformio/packages, if present."""
    home = Path.home()
    patterns = [
        home / ".platformio/packages/toolchain-xtensa-esp32s3*/bin/xtensa-esp32s3-elf-addr2line",
        home / ".platformio/packages/toolchain-xtensa-esp-elf*/bin/xtensa-esp32s3-elf-addr2line",
        home / ".platformio/packages/toolchain-xtensa-esp-elf*/bin/xtensa-esp32-elf-addr2line",
        home / ".platformio/packages/toolchain-xtensa-esp-elf*/bin/xtensa-esp-elf-addr2line",
    ]
    for pat in patterns:
        matches = sorted(glob.glob(str(pat)))
        if matches:
            return matches[0]
    return None


def symbolize(
    addrs: Sequence[str], elf: str, addr2line: Optional[str] = None
) -> Dict[str, str]:
    """Resolve raw address strings (e.g. '0x1234') to 'function (file:line)'.

    Falls back to the raw address string for any input (and for everything,
    if the tool or elf can't be found, or the batch output shape doesn't
    match the input) -- symbolization is a nice-to-have, never a hard
    requirement for the harness to run.
    """
    uniq = list(dict.fromkeys(addrs))
    result: Dict[str, str] = {a: a for a in uniq}
    if not uniq:
        return result
    tool = addr2line or find_addr2line()
    if not tool or not Path(elf).exists():
        return result
    try:
        proc = subprocess.run(
            [tool, "-pfaC", "-e", elf, *uniq],
            capture_output=True,
            text=True,
            timeout=20,
        )
    except (OSError, subprocess.SubprocessError):
        return result
    if proc.returncode != 0:
        return result
    out_lines = [l for l in proc.stdout.splitlines() if l.strip()]
    if len(out_lines) != len(uniq):
        return result
    for addr, resolved in zip(uniq, out_lines):
        text = resolved.split(": ", 1)[1] if ": " in resolved else resolved
        result[addr] = text.strip()
    return result


# --------------------------------------------------------------------------
# Scenario helpers
# --------------------------------------------------------------------------


def get_uistat(session: TDeckSession, timeout: float = 2.0) -> Optional[Dict[str, Any]]:
    idx = session.send("--uistat")
    m = session.wait_for(r"\[UISTAT\]", timeout, since=idx)
    if not m:
        return None
    return parse_line(m.string)


def get_screencrc(session: TDeckSession, timeout: float = 2.0) -> Optional[Dict[str, Any]]:
    idx = session.send("--screencrc")
    m = session.wait_for(r"\[SCREEN\]", timeout, since=idx)
    if not m:
        return None
    return parse_line(m.string)


def _parsed_range(session: TDeckSession, start: int, end: int) -> List[Dict[str, Any]]:
    raw = session.records_range(start, end)
    out = []
    for _, _, line in raw:
        rec = parse_line(line)
        if rec:
            out.append(rec)
    return out


# --------------------------------------------------------------------------
# Scenarios
# --------------------------------------------------------------------------


def _short_sym(sym: str) -> str:
    """'func(args) at /long/path/file.cpp:123' -> 'func file.cpp:123'."""
    if " at " not in sym:
        return sym
    func, loc = sym.split(" at ", 1)
    func = func.split("(", 1)[0]
    loc = re.sub(r"\s*\(discriminator \d+\)", "", loc)
    return f"{func} {loc.rsplit('/', 1)[-1]}"


def _screencrc(session: TDeckSession) -> Optional[List[str]]:
    idx = session.send("--screencrc")
    m = session.wait_for(r"\[SCREEN\];ms;", 4.0, since=idx)
    rec = parse_line(m.string) if m else None
    return rec.get("crc") if rec else None


def scenario_idle(session: TDeckSession, args: argparse.Namespace) -> Dict[str, Any]:
    pre = get_uistat(session)
    idx = session.send("--instreset")
    session.wait_for(r"\[INSTR", 2.0, since=idx)
    session.send("--redrawlog on")
    time.sleep(0.2)

    start = session.length()
    time.sleep(args.idle_seconds)
    end = session.length()

    session.send("--redrawlog off")
    time.sleep(0.2)
    instr_idx = session.send("--instr")
    instr_lines = session.collect(1.0, since=instr_idx)
    post = get_uistat(session)

    parsed = _parsed_range(session, start, end)
    rsum = redraw_summary(parsed)
    fsum = refr_summary(parsed, window_seconds=args.idle_seconds)

    top_cls_name = sorted(rsum["by_cls_name"].items(), key=lambda kv: -kv[1])[:10]
    top_ra = sorted(rsum["by_ra"].items(), key=lambda kv: -kv[1])[:10]
    # Group by full backtrace: the ra alone is usually lv_obj_invalidate itself.
    bt_counts: Dict[Tuple[str, ...], int] = {}
    bt_objs: Dict[Tuple[str, ...], set] = {}
    for rec in parsed:
        if rec and rec.get("kind") == "REDRAW" and rec.get("variant") == "obj":
            key = tuple(rec.get("bt") or ())
            bt_counts[key] = bt_counts.get(key, 0) + 1
            bt_objs.setdefault(key, set()).add(rec.get("obj"))
    top_bt = sorted(bt_counts.items(), key=lambda kv: -kv[1])[:10]
    addrs = sorted({ra for ra, _ in top_ra if ra} | {a for key, _ in top_bt for a in key})
    sym_map = symbolize(addrs, args.elf) if addrs else {}

    flush = None
    loop = None
    for line in instr_lines:
        rec = parse_line(line)
        if rec and rec.get("kind") == "INSTR-FLUSH" and flush is None:
            flush = rec
        elif rec and rec.get("kind") == "INSTR-LOOP" and loop is None:
            loop = rec

    return {
        "ok": True,  # measurement-only scenario; no pass/fail criterion is defined
        "pre_uistat": pre,
        "post_uistat": post,
        "window_seconds": args.idle_seconds,
        "redraw_total": rsum["total"],
        "redraw_dropped": rsum["dropped"],
        "invalidations_per_second": rsum["total"] / args.idle_seconds
        if args.idle_seconds
        else None,
        "refr": fsum,
        "flush": flush,
        "loop": loop,
        "top10_invalidators_by_class": [
            {"cls": cls, "name": name, "count": cnt} for (cls, name), cnt in top_cls_name
        ],
        "top10_invalidators_by_ra": [
            {"ra": ra, "symbol": sym_map.get(ra, ra), "count": cnt} for ra, cnt in top_ra
        ],
        "top10_invalidators_by_backtrace": [
            {
                "count": cnt,
                "objects": len(bt_objs.get(key, ())),
                "frames": [sym_map.get(a, a) for a in key],
            }
            for key, cnt in top_bt
        ],
    }


def scenario_tabs(session: TDeckSession, args: argparse.Namespace) -> Dict[str, Any]:
    list_idx = session.send("--tab list")
    session.wait_for(r"\[TAB\]", 1.0, since=list_idx)
    listing = session.collect(0.3, since=list_idx)

    per_tab = []
    ok = True
    for i in range(8):
        session.send("--redrawlog on")
        time.sleep(0.1)
        set_idx = session.send(f"--tab {i}")
        set_match = session.wait_for(r"\[TAB\];set;", 1.0, since=set_idx)
        refr_match = session.wait_for(r"\[REFR\]", 0.5, since=set_idx)
        session.collect(1.5)  # fill the rest of the ~2s observation window
        end = session.length()
        session.send("--redrawlog off")
        time.sleep(0.1)
        post = get_uistat(session)

        parsed = _parsed_range(session, set_idx, end)
        rsum = redraw_summary(parsed)
        fsum = refr_summary(parsed, window_seconds=2.0)
        repainted = refr_match is not None
        per_tab.append(
            {
                "idx": i,
                "tab_set_seen": set_match is not None,
                "inv_count": rsum["total"],
                "refr_count": fsum["count"],
                "px": fsum["sum_px"],
                "repainted_within_500ms": repainted,
                "post_uistat": post,
            }
        )
        if not repainted:
            ok = False

    return {"ok": ok, "tab_listing": listing, "per_tab": per_tab}


def scenario_drawer(session: TDeckSession, args: argparse.Namespace) -> Dict[str, Any]:
    per_toggle = []
    ok = True
    for rep in range(3):
        for state, cmd in ((1, "--drawer on"), (0, "--drawer off")):
            session.send("--redrawlog on")
            time.sleep(0.1)
            cmd_idx = session.send(cmd)
            ack = session.wait_for(rf"\[DRAWER\];{state}\b", 1.0, since=cmd_idx)
            session.collect(1.4)  # fill the rest of the ~1.5s observation window
            end = session.length()
            session.send("--redrawlog off")
            time.sleep(0.1)
            post = get_uistat(session)

            parsed = _parsed_range(session, cmd_idx, end)
            rsum = redraw_summary(parsed)
            fsum = refr_summary(parsed, window_seconds=1.5)
            acked = ack is not None
            per_toggle.append(
                {
                    "rep": rep,
                    "state": state,
                    "drawer_ack": acked,
                    "inv_count": rsum["total"],
                    "refr_count": fsum["count"],
                    "px": fsum["sum_px"],
                    "post_uistat": post,
                }
            )
            if not acked:
                ok = False

    return {"ok": ok, "per_toggle": per_toggle}


def scenario_inject(session: TDeckSession, args: argparse.Namespace) -> Dict[str, Any]:
    session.send("--tab 0")
    time.sleep(0.3)
    prev = get_uistat(session)
    prev_msg_list = prev.get("msg_list") if prev else None

    results = []
    ok = True
    prev_refr_total = prev.get("refr_total") if prev else None
    for i in range(args.inject_count):
        t_start = time.monotonic()
        session.send("--redrawlog on")
        time.sleep(0.15)
        idx = session.send(f"--injectmsg {TEST_GROUP} bench inject {i}")
        inject_ok = session.wait_for(r"\[INJECT\];ok", 2.0, since=idx) is not None
        refr_seen = session.wait_for(r"\[REFR\]", 1.5, since=idx) is not None
        time.sleep(0.5)
        end = session.length()
        session.send("--redrawlog off")
        time.sleep(0.15)
        window = [l for _, _, l in session.records_since(idx)[: end - idx]]
        audio_lines = [l for l in window if "[AUDIO]" in l]
        parsed = [parse_line(l) for l in window]
        rsum = redraw_summary(parsed)
        bt_counts: Dict[Tuple[str, ...], int] = {}
        for rec in parsed:
            if rec and rec.get("kind") == "REDRAW" and rec.get("variant") == "obj":
                key = tuple(rec.get("bt") or ())
                bt_counts[key] = bt_counts.get(key, 0) + 1
        top_bt = sorted(bt_counts.items(), key=lambda kv: -kv[1])[:8]
        sym_map = symbolize(sorted({a for key, _ in top_bt for a in key}), args.elf) if top_bt else {}
        # NOTE: --screencrc readback returns a constant on this hardware (MISO not
        # driven by the panel), so no panel-level fingerprint is taken here.
        panel_updated = None
        crc_after = None
        crc_forced = None
        post = get_uistat(session)
        msg_list = post.get("msg_list") if post else None
        refr_total = post.get("refr_total") if post else None
        growth = (
            msg_list - prev_msg_list
            if (msg_list is not None and prev_msg_list is not None)
            else None
        )
        refr_delta = (
            refr_total - prev_refr_total
            if (refr_total is not None and prev_refr_total is not None)
            else None
        )
        repainted = refr_seen or (refr_delta is not None and refr_delta > 0)
        displayed = inject_ok and repainted
        results.append(
            {
                "i": i,
                "inject_ok": inject_ok,
                "refr_seen": refr_seen,
                "refr_delta": refr_delta,
                "displayed": displayed,
                "msg_list": msg_list,
                "msg_list_growth": growth,
                "inv_count": rsum["total"],
                "panel_updated": panel_updated,
                "crc_after": crc_after,
                "crc_forced": crc_forced,
                "top_backtraces": [
                    {"count": cnt, "frames": [_short_sym(sym_map.get(a, a)) for a in key]}
                    for key, cnt in top_bt
                ],
                "audio_lines": audio_lines,
            }
        )
        prev_refr_total = refr_total
        if not displayed:
            ok = False
        prev_msg_list = msg_list

        elapsed = time.monotonic() - t_start
        remaining = args.inject_spacing - elapsed
        if remaining > 0 and i < args.inject_count - 1:
            time.sleep(remaining)

    return {"ok": ok, "results": results}


def scenario_audio(session: TDeckSession, args: argparse.Namespace) -> Dict[str, Any]:
    cases = [
        ("start", r"\[AUDIO\];play"),
        ("msg", r"\[AUDIO\];play"),
        ("/nonexistent.wav", r"\[AUDIO\];err;missing"),
    ]
    results = []
    ok = True
    for what, pattern in cases:
        idx = session.send(f"--playtone {what}")
        # Audio requests are queued and played one after another in the audio
        # task; a result line can arrive only after the tones queued before it
        # have finished (start 1.5 s + msg 1.1 s), so wait longer than that.
        m = session.wait_for(pattern, 8.0, since=idx)
        passed = m is not None
        results.append(
            {
                "what": what,
                "expected_pattern": pattern,
                "matched_line": m.string if m else None,
                "passed": passed,
            }
        )
        if not passed:
            ok = False
    return {"ok": ok, "results": results}


def _instr_loop_and_flush(lines: Sequence[str]) -> Tuple[Optional[Dict[str, Any]], Optional[Dict[str, Any]]]:
    loop = None
    flush = None
    for line in lines:
        rec = parse_line(line)
        if rec and rec.get("kind") == "INSTR-LOOP" and loop is None:
            loop = rec
        elif rec and rec.get("kind") == "INSTR-FLUSH" and flush is None:
            flush = rec
    return loop, flush


def scenario_audio_stall(session: TDeckSession, args: argparse.Namespace) -> Dict[str, Any]:
    """Does audio playback starve the main loop / LVGL? Operator report: UI freezes
    while a tone plays, so this scenario is expected to FAIL today -- it must
    measure, not pass.
    """
    # with audio playing
    reset_idx = session.send("--instreset")
    session.wait_for(r"\[INSTR", 2.0, since=reset_idx)
    play_idx = session.send("--playtone msg")
    play_seen = session.wait_for(r"\[AUDIO\];play;msg", 2.0, since=play_idx) is not None
    time.sleep(2.5)
    instr_idx = session.send("--instr")
    instr_lines = session.collect(1.0, since=instr_idx)
    loop_with_audio, flush_with_audio = _instr_loop_and_flush(instr_lines)

    # control: idle, no audio
    reset2_idx = session.send("--instreset")
    session.wait_for(r"\[INSTR", 2.0, since=reset2_idx)
    time.sleep(2.5)
    instr2_idx = session.send("--instr")
    instr2_lines = session.collect(1.0, since=instr2_idx)
    loop_idle, flush_idle = _instr_loop_and_flush(instr2_lines)

    loop_max_us_with_audio = loop_with_audio.get("max_us") if loop_with_audio else None
    loop_max_us_idle = loop_idle.get("max_us") if loop_idle else None
    ratio = (
        loop_max_us_with_audio / loop_max_us_idle
        if (loop_max_us_with_audio is not None and loop_max_us_idle)
        else None
    )
    ok = loop_max_us_with_audio is not None and loop_max_us_with_audio < 100000

    return {
        "ok": ok,
        "play_seen": play_seen,
        "loop_with_audio": loop_with_audio,
        "flush_with_audio": flush_with_audio,
        "loop_idle": loop_idle,
        "flush_idle": flush_idle,
        "loop_max_us_with_audio": loop_max_us_with_audio,
        "loop_max_us_idle": loop_max_us_idle,
        "ratio": ratio,
    }


# --------------------------------------------------------------------------
# map: foreign stations at growing distances, full zoom sweeps
# --------------------------------------------------------------------------

CRASH_PATTERN = r"Guru Meditation|rst:0x|Backtrace:|abort\(\)|assert failed"

# Distances in km and bearings in degrees for the injected stations; at the
# default zoom the near ones share the viewport with the own position, the far
# ones are outside it and only come into view when zooming out.
MAP_STATIONS = [
    (0.3, 45), (0.8, 135), (1.5, 225), (3.0, 315), (6.0, 20),
    (12.0, 110), (25.0, 200), (60.0, 290), (150.0, 60), (400.0, 240),
]


def _own_position(session: TDeckSession) -> Optional[Tuple[float, float]]:
    """--pos prints '...LAT: 48.4076 N' / '...LON: 11.7386 E'."""
    idx = session.send("--pos")
    if session.wait_for(r"LON:", 4.0, since=idx) is None:
        return None
    lat = lon = None
    for _, _, l in session.records_since(idx):
        m = re.search(r"LAT:\s*([0-9.]+)\s*([NS])", l)
        if m:
            lat = float(m.group(1)) * (-1 if m.group(2) == "S" else 1)
        m = re.search(r"LON:\s*([0-9.]+)\s*([EW])", l)
        if m:
            lon = float(m.group(1)) * (-1 if m.group(2) == "W" else 1)
    if lat is None or lon is None or (lat == 0.0 and lon == 0.0):
        return None
    return lat, lon


def _offset(lat: float, lon: float, km: float, bearing_deg: float) -> Tuple[float, float]:
    b = math.radians(bearing_deg)
    dlat = km * math.cos(b) / 111.0
    dlon = km * math.sin(b) / (111.0 * max(0.1, math.cos(math.radians(lat))))
    return lat + dlat, lon + dlon


def _zoom_step(session: TDeckSession, direction: str) -> Dict[str, Any]:
    session.send("--tft on")            # keep the panel lit for the eye test
    time.sleep(0.1)
    idx = session.send(f"--mapzoom {direction}")
    m = session.wait_for(r"\[MAPZOOM\];|" + CRASH_PATTERN, 10.0, since=idx)
    step: Dict[str, Any] = {"dir": direction, "acked": False, "zoom": None, "crash": None,
                            "center_err": None, "compose_ms": None}
    if m is None:
        return step
    if "MAPZOOM" not in m.string:
        step["crash"] = m.string.strip()[:120]
        return step
    step["acked"] = True
    step["zoom"] = int(m.string.strip().split(";")[-1])
    # the centring line and the compose line follow the zoom ack
    time.sleep(1.2)
    for _, _, l in session.records_since(idx):
        mm = re.search(r"\[MAP\];zoom;(\d+);.*center_err;(-?\d+);(-?\d+)", l)
        if mm:
            step["center_err"] = (int(mm.group(2)), int(mm.group(3)))
        mm = re.search(r"Karte zusammengesetzt: zoom (\d+), Kacheln (\d+) \(fehlend (\d+)\).*?, (\d+) ms", l)
        if mm:
            step["compose_ms"] = int(mm.group(4))
            step["tiles"] = int(mm.group(2))
            step["tiles_missing"] = int(mm.group(3))
    return step


def _sweep(session: TDeckSession, direction: str, max_steps: int) -> List[Dict[str, Any]]:
    """Zoom in one direction until the reported zoom stops changing (bound reached)."""
    steps: List[Dict[str, Any]] = []
    last = None
    for _ in range(max_steps):
        st = _zoom_step(session, direction)
        steps.append(st)
        if st["crash"] or not st["acked"]:
            break
        if st["zoom"] == last:
            break
        last = st["zoom"]
    return steps


def scenario_map(session: TDeckSession, args: argparse.Namespace) -> Dict[str, Any]:
    """The real map test: ten foreign stations at 0.3-400 km around the own
    position (some outside the current viewport), then a full zoom-in sweep to
    the map set's upper bound and a full zoom-out sweep to its lower bound.
    Verdict: every injection acknowledged, every zoom step acknowledged, no
    crash, own position centred (center_err 0/0) at every zoom level, both
    bounds reached. Whether the red dots are drawn is the operator's eye test.
    """
    home = _own_position(session)
    if home is None:
        return {"ok": False, "reason": "no own position (--pos gave 0/0 or no answer)"}
    lat, lon = home

    session.send("--tft on")
    session.send("--tab 3")
    time.sleep(2.0)
    before = get_uistat(session)

    stations = []
    inject_ok = True
    # Beyond the ten table entries the extra stations sit within 1 km on a
    # spiral, i.e. inside the viewport at the default zoom -- off-screen
    # stations take no marker slot, so only on-screen ones can push the ring
    # past MAX_POINTS (30) into the slot-recycling branch of add_map_point()
    # (UP-02). --map-stations 40 recycles a handful of slots.
    plan = list(MAP_STATIONS[: args.map_stations])
    for i in range(len(plan), args.map_stations):
        k = i - len(MAP_STATIONS)
        plan.append((0.1 + 0.03 * k, (k * 47) % 360))
    session.send("--instreset")
    time.sleep(0.2)
    for i, (km, bearing) in enumerate(plan, start=1):
        slat, slon = _offset(lat, lon, km, bearing)
        call = f"DK5EM-{i:02d}"  # not DK5EN: DK5EN-14 is the bench node itself
        idx = session.send(f"--injectpos {call} {slat:.5f} {slon:.5f}")
        m = session.wait_for(r"\[INJECTPOS\];(ok|err)|" + CRASH_PATTERN, 6.0, since=idx)
        ok = m is not None and "INJECTPOS];ok" in m.string
        stations.append({"call": call, "km": km, "bearing": bearing, "lat": round(slat, 5),
                         "lon": round(slon, 5), "ok": ok,
                         "line": m.string.strip()[:100] if m else None})
        if not ok:
            inject_ok = False
        time.sleep(0.6)

    idx = session.send("--instr")
    m_loop = session.wait_for(r"\[INSTR-LOOP\];", 2.0, since=idx)
    m_gui = session.wait_for(r"\[INSTR-GUI\];", 2.0, since=idx)
    loop_rec = parse_line(m_loop.string) if m_loop else None
    gui_rec = parse_line(m_gui.string) if m_gui else None
    inject_loop_max_us = loop_rec.get("max_us") if loop_rec else None
    map_points_after = gui_rec.get("map_points") if gui_rec else None

    zoom_in = _sweep(session, "in", 12)
    zoom_out = _sweep(session, "out", 24)
    zoom_back = _sweep(session, "in", 24)
    steps = zoom_in + zoom_out + zoom_back

    crashed = next((st["crash"] for st in steps if st["crash"]), None)
    all_acked = all(st["acked"] for st in steps)
    zooms = [st["zoom"] for st in steps if st["zoom"] is not None]
    centred = [st for st in steps if st["center_err"] is not None]
    off_centre = [st for st in centred if st["center_err"] != (0, 0)]
    after = get_uistat(session)

    ok = (
        inject_ok
        and crashed is None
        and all_acked
        and len(zooms) >= 2
        and max(zooms) > min(zooms)
        and len(centred) > 0
        and not off_centre
    )
    return {
        "ok": ok,
        "home": {"lat": lat, "lon": lon},
        "stations": stations,
        "inject_loop_max_us": inject_loop_max_us,
        "map_points_after": map_points_after,
        "zoom_min": min(zooms) if zooms else None,
        "zoom_max": max(zooms) if zooms else None,
        "steps": steps,
        "steps_total": len(steps),
        "steps_acked": sum(1 for st in steps if st["acked"]),
        "centring_lines": len(centred),
        "off_centre_steps": [(st["dir"], st["zoom"], st["center_err"]) for st in off_centre],
        "compose_ms_max": max((st["compose_ms"] for st in steps if st["compose_ms"] is not None), default=None),
        "crashed": crashed,
        "uistat_before": before,
        "uistat_after": after,
    }


# --------------------------------------------------------------------------
# nav: drawer -> tab -> drawer over every tab, scroll the settings page
# --------------------------------------------------------------------------

TAB_COUNT = 8
SETTINGS_TAB = 7


def _acked_step(session: TDeckSession, cmd: str, ack_pattern: str, timeout: float = 2.0) -> Dict[str, Any]:
    """Send one console command with the redraw log on; report ack, repaint
    and crash lines for it."""
    session.send("--tft on")            # panel darkens 30 s after the last key; keep the eye test lit
    session.send("--redrawlog on")
    time.sleep(0.1)
    idx = session.send(cmd)
    ack = session.wait_for(ack_pattern + "|" + CRASH_PATTERN, timeout, since=idx)
    refr = session.wait_for(r"\[REFR\]", 1.0, since=idx)
    session.collect(0.4)
    end = session.length()
    session.send("--redrawlog off")
    time.sleep(0.1)
    parsed = _parsed_range(session, idx, end)
    fsum = refr_summary(parsed, window_seconds=1.5)
    crashed = ack is not None and re.search(CRASH_PATTERN, ack.string) is not None
    return {
        "cmd": cmd,
        "acked": ack is not None and not crashed,
        "ack_line": ack.string.strip()[:120] if ack else None,
        "repainted": refr is not None,
        "refr_count": fsum["count"],
        "px": fsum["sum_px"],
        "crashed": crashed,
    }


def scenario_nav(session: TDeckSession, args: argparse.Namespace) -> Dict[str, Any]:
    """Operator's navigation pattern: open the drawer, pick the tab, close the
    drawer -- over all eight tabs -- and on the (long) settings page scroll to
    the bottom and back to the top. Every step must be acknowledged and
    repainted, nothing may crash, and the settings page must actually move."""
    session.send("--tft on")
    steps: List[Dict[str, Any]] = []
    for i in range(TAB_COUNT):
        steps.append(_acked_step(session, "--drawer on", r"\[DRAWER\];1\b"))
        steps.append(_acked_step(session, f"--tab {i}", r"\[TAB\];set;"))
        steps.append(_acked_step(session, "--drawer off", r"\[DRAWER\];0\b"))
        if i == SETTINGS_TAB:
            for direction in (+1, -1):
                last_y = None
                for _ in range(40):
                    st = _acked_step(session, f"--scroll {SETTINGS_TAB} {direction * 120}", r"\[SCROLL\];")
                    steps.append(st)
                    if st["crashed"] or not st["acked"]:
                        break
                    m = re.search(r"y;(-?\d+);(-?\d+);bottom;(-?\d+)", st["ack_line"] or "")
                    y_after = int(m.group(2)) if m else None
                    st["scroll_y"] = y_after
                    if y_after is None or y_after == last_y:
                        break                       # bound reached
                    last_y = y_after
        if any(st["crashed"] for st in steps[-3:]):
            break
    scroll_ys = [st["scroll_y"] for st in steps if st.get("scroll_y") is not None]
    crashed = next((st["ack_line"] for st in steps if st["crashed"]), None)
    not_acked = [st["cmd"] for st in steps if not st["acked"]]
    not_repainted = [st["cmd"] for st in steps if st["acked"] and not st["repainted"]]
    post = get_uistat(session)
    ok = (
        crashed is None
        and not not_acked
        and not not_repainted
        and len(scroll_ys) >= 2
        and max(scroll_ys) > 0
        and scroll_ys[-1] == 0
    )
    return {
        "ok": ok,
        "steps_total": len(steps),
        "steps_acked": sum(1 for st in steps if st["acked"]),
        "not_acked": not_acked,
        "not_repainted": not_repainted,
        "settings_scroll_max": max(scroll_ys) if scroll_ys else None,
        "settings_scroll_final": scroll_ys[-1] if scroll_ys else None,
        "crashed": crashed,
        "steps": steps,
        "uistat_after": post,
    }


# --------------------------------------------------------------------------
# input: keyboard and trackball through the LVGL indev chain
# --------------------------------------------------------------------------

def _latency_ms(session: TDeckSession, since: int, event_pat: str, until_pat: str) -> List[float]:
    """For every event line matching event_pat after `since`, the time to the
    next line matching until_pat (a repaint), in ms, from the host timestamps."""
    recs = session.records_since(since)
    out: List[float] = []
    pending: Optional[float] = None
    for _, t_mono, line in recs:
        if re.search(event_pat, line):
            pending = t_mono
        elif pending is not None and re.search(until_pat, line):
            out.append((t_mono - pending) * 1000.0)
            pending = None
    return out


def _pct(values: Sequence[float], q: float) -> Optional[float]:
    if not values:
        return None
    v = sorted(values)
    k = min(len(v) - 1, int(round(q * (len(v) - 1))))
    return round(v[k], 1)


def scenario_input(session: TDeckSession, args: argparse.Namespace) -> Dict[str, Any]:
    """Keyboard and trackball tests. Keys are injected into keypad_get_key()
    (the I2C keyboard path) on the chat tab, trackball steps into mouse_read()
    (the GPIO edge path); the firmware logs every consumed [KEY]/[BALL] event
    and the redraw log shows the repaint. Verdict: every injected key and
    every trackball step is consumed and followed by a repaint; the
    event-to-repaint latency is reported (p50/p95), and the trackball's
    cursor must move by exactly one step per event -- the "not smooth"
    symptom is an event that is swallowed or a repaint that lags."""
    session.send("--tft on")
    session.send("--tab 1")               # chat input tab
    time.sleep(1.0)

    # -- keyboard
    text = args.input_text
    session.send("--redrawlog on")
    time.sleep(0.1)
    idx = session.send(f"--key {text}")
    session.wait_for(r"\[KEY\];inject;", 2.0, since=idx)
    time.sleep(0.5 + 0.05 * len(text))
    key_lines = [l for _, _, l in session.records_since(idx) if re.search(r"\[KEY\];[0-9a-f]{2};", l)]
    key_lat = _latency_ms(session, idx, r"\[KEY\];[0-9a-f]{2};", r"\[REFR\]")
    session.send("--redrawlog off")
    time.sleep(0.1)
    # clear what we typed
    session.send("--key " + "\\b" * len(text))
    time.sleep(0.5)

    # -- trackball: move the cursor away from the screen edge (it starts at
    # 0/0 and mouse_read() clamps at the borders), then walk a square.
    session.send(f"--tab {args.input_tab}")
    time.sleep(0.5)
    session.send("--ball right 14")
    time.sleep(0.5)
    session.send("--ball down 8")
    time.sleep(0.5)
    ball_results = []
    for direction, n in (("right", args.input_ball_steps), ("down", args.input_ball_steps),
                         ("left", args.input_ball_steps), ("up", args.input_ball_steps)):
        session.send("--redrawlog on")
        time.sleep(0.1)
        # inject like a hand rolls: a few edges per indev read, not the whole
        # request in one 10 ms read (mouse_read() consumes at most
        # BALL_MAX_STEPS_PER_READ per read and discards the rest -- that is
        # the stall protection, see TM-18)
        idx = session.length()
        remaining = n
        while remaining > 0:
            chunk = min(3, remaining)
            session.send(f"--ball {direction} {chunk}")
            remaining -= chunk
            time.sleep(0.06)
        session.wait_for(r"\[BALL\];inject;", 2.0, since=idx)
        time.sleep(0.4)
        # One [BALL] line per indev read with activity; in edge mode a read
        # consumes up to 4 counted edges, so a line carries `steps;k`. The
        # contract: the steps add up to the request, and the cursor moved
        # exactly 10 px per step along one axis (no lost, no doubled edges).
        events = []
        steps_total = 0
        for _, _, l in session.records_since(idx):
            m = re.search(r"\[BALL\];x;(-?\d+);y;(-?\d+);btn;(\d)(?:;steps;(\d+))?", l)
            if m:
                events.append((int(m.group(1)), int(m.group(2))))
                steps_total += int(m.group(4)) if m.group(4) else 1
        lat = _latency_ms(session, idx, r"\[BALL\];x;", r"\[REFR\]")
        refr_ms = []
        for _, _, l in session.records_since(idx):
            m = re.search(r"\[REFR\];.*?;ms;(\d+)", l)
            if m:
                refr_ms.append(float(m.group(1)))
        session.send("--redrawlog off")
        time.sleep(0.1)
        moved = 0
        if len(events) >= 2:
            (x0, y0), (x1, y1) = events[0], events[-1]
            moved = abs(x1 - x0) + abs(y1 - y0)
        # displacement between first and last line misses the first line's own
        # steps; count them from that line's step field
        first_steps = 0
        for _, _, l in session.records_since(idx):
            m = re.search(r"\[BALL\];.*steps;(\d+)", l)
            if m:
                first_steps = int(m.group(1))
                break
        moved_expected = 10 * (steps_total - first_steps)
        ball_results.append({
            "dir": direction, "requested": n, "steps": steps_total, "events": len(events),
            "moved_px": moved, "moved_expected_px": moved_expected,
            "first": events[0] if events else None, "last": events[-1] if events else None,
            "latency_p50_ms": _pct(lat, 0.5), "latency_p95_ms": _pct(lat, 0.95),
            "refr_ms_p50": _pct(refr_ms, 0.5), "refr_ms_max": max(refr_ms) if refr_ms else None,
            "repaints": len(lat),
        })

    # Keys are consumed one per indev read (10 ms); several land inside one
    # refresh period, so "one repaint per key" is not the contract -- every
    # key consumed and at least one repaint after the burst is.
    keys_ok = len(key_lines) == len(text) and len(key_lat) >= 1
    ball_ok = all(
        r["steps"] == r["requested"]
        and r["repaints"] == r["events"]
        and r["moved_px"] == r["moved_expected_px"]
        for r in ball_results
    )
    ok = keys_ok and ball_ok
    return {
        "ok": ok,
        "keys_sent": len(text),
        "keys_consumed": len(key_lines),
        "key_repaints": len(key_lat),
        "key_latency_p50_ms": _pct(key_lat, 0.5),
        "key_latency_p95_ms": _pct(key_lat, 0.95),
        "ball": ball_results,
    }


# --------------------------------------------------------------------------
# msg_roll: cursor stall after messages arrived and the drawer was collapsed
# --------------------------------------------------------------------------

def _instr_snapshot(session: TDeckSession) -> Dict[str, Any]:
    """One --instr readout: loop n/total/max and per-section n/total/max.
    The counters accumulate since boot (or the last instrument_reset()), so
    two snapshots give a per-phase average by difference."""
    idx = session.send("--instr")
    session.wait_for(r"\[INSTR-GAPS\]", 3.0, since=idx)
    time.sleep(0.2)
    snap: Dict[str, Any] = {"loop": None, "sect": {}}
    for _, _, l in session.records_since(idx):
        m = re.search(r"\[INSTR-LOOP\][; ]n[; ](\d+)[; ]total_us[; ](\d+)[; ]avg_us[; ]\d+[; ]max_us[; ](\d+)", l)
        if m:
            snap["loop"] = (int(m.group(1)), int(m.group(2)), int(m.group(3)))
            continue
        m = re.search(r"\[INSTR-SECT\][; ](\w+)[; ]n[; ](\d+)[; ]total_us[; ](\d+)[; ]avg_us[; ]\d+[; ]max_us[; ](\d+)", l)
        if m:
            snap["sect"][m.group(1)] = (int(m.group(2)), int(m.group(3)), int(m.group(4)))
    return snap


def _instr_delta(a: Dict[str, Any], b: Dict[str, Any]) -> Dict[str, Any]:
    out: Dict[str, Any] = {"loop_avg_us": None, "loop_n": None, "sect_avg_us": {}, "sect_share_pct": {}}
    if a.get("loop") and b.get("loop"):
        dn = b["loop"][0] - a["loop"][0]
        dus = b["loop"][1] - a["loop"][1]
        out["loop_n"] = dn
        out["loop_avg_us"] = int(dus / dn) if dn > 0 else None
        for name, (n1, us1, _) in b["sect"].items():
            n0, us0, _ = a["sect"].get(name, (0, 0, 0))
            sn, sus = n1 - n0, us1 - us0
            if sn > 0:
                out["sect_avg_us"][name] = int(sus / sn)
                out["sect_share_pct"][name] = round(100.0 * sus / dus, 1) if dus > 0 else None
    return out


# --------------------------------------------------------------------------
# cdc_backpressure: prints must not block the main loop when nobody reads
# --------------------------------------------------------------------------

def scenario_cdc_backpressure(session: TDeckSession, args: argparse.Namespace) -> Dict[str, Any]:
    """Operator report 2026-09-05 (CDC-01): once the USB serial connection is
    gone (cable pulled, terminal closed) the trackball cursor and the touch
    input freeze in regular intervals. arduino-esp32 2.0.14 HWCDC sets its TX
    timeout to 100 ms on the first successful host read and never takes it
    back, so every Serial print that does not fit the 256 B ring buffer
    blocks the main loop for 100 ms per write once the host stops draining.
    Instrument: keep the port open but stop reading it for --cdc-pause-seconds
    while the node keeps printing ([BALL] per cursor step under --redrawlog,
    GPS lines every 3 s). Verdict: no [INSTR-LOOP] gap and no rise of the
    [INSTR-GAPS] counter during the pause, and the loop average must not
    grow more than 1.5x against the control roll."""
    session.send("--tft on")
    session.send("--tab 0")
    time.sleep(0.5)
    session.send("--ball left 40")
    time.sleep(0.3)
    session.send("--ball right 8")
    time.sleep(0.3)

    def roll(seconds: float) -> int:
        t0 = time.monotonic()
        k = 0
        while time.monotonic() - t0 < seconds:
            session.send(f"--ball {'right' if (k // 4) % 2 == 0 else 'left'} 3")
            k += 1
            time.sleep(0.2)
        return k

    # control: host reads, redraw log on (same print load as the pause phase)
    session.send("--redrawlog on")
    time.sleep(0.2)
    c0 = _instr_snapshot(session)
    gaps0 = _instr_gap_count(session)
    idx_c = session.length()
    roll(args.cdc_control_seconds)
    time.sleep(0.5)
    c1 = _instr_snapshot(session)
    gaps_c = _instr_gap_count(session) - gaps0
    control = _instr_delta(c0, c1)
    control_gap_lines = [l.strip()[:120] for _, _, l in session.records_since(idx_c) if re.search(r"\[INSTR-LOOP\][; ]gap", l)]

    # pause: host stops reading, node keeps printing
    p0 = _instr_snapshot(session)
    gaps_before = _instr_gap_count(session)
    idx_p = session.length()
    session.pause_reader(True)
    sent = roll(args.cdc_pause_seconds)
    session.pause_reader(False)
    time.sleep(3.0)                     # drain the backlog the host held back
    session.send("--redrawlog off")
    time.sleep(0.3)
    p1 = _instr_snapshot(session)
    gaps_after = _instr_gap_count(session)
    paused = _instr_delta(p0, p1)
    pause_gap_lines = [l.strip()[:120] for _, _, l in session.records_since(idx_p) if re.search(r"\[INSTR-LOOP\][; ]gap", l)]
    gaps_p = gaps_after - gaps_before
    slowdown = None
    if control.get("loop_avg_us") and paused.get("loop_avg_us"):
        slowdown = round(paused["loop_avg_us"] / control["loop_avg_us"], 2)
    ok = gaps_p == 0 and not pause_gap_lines and (slowdown is None or slowdown < 1.5)
    return {
        "ok": ok,
        "control_gap_count": gaps_c,
        "control_gap_lines": control_gap_lines[:5],
        "control_loop_avg_us": control.get("loop_avg_us"),
        "pause_seconds": args.cdc_pause_seconds,
        "pause_cmds_sent": sent,
        "pause_gap_count": gaps_p,
        "pause_gap_lines": pause_gap_lines[:10],
        "pause_loop_avg_us": paused.get("loop_avg_us"),
        "pause_loop_n": paused.get("loop_n"),
        "loop_slowdown_x": slowdown,
    }


def _instr_gap_count(session: TDeckSession) -> int:
    idx = session.send("--instr")
    m = session.wait_for(r"\[INSTR-GAPS\][; ]n[; ](\d+)", 3.0, since=idx)
    time.sleep(0.2)
    return int(m.group(1)) if m else -1


def _roll_phase(session: TDeckSession, seconds: float, gap_ms: int, cadence_ms: int = 200) -> Dict[str, Any]:
    """Roll the trackball for `seconds` (3-step chunks, like a hand rolls) and
    measure: main-loop gaps attributed to the lvgl section, gaps between
    consecutive [BALL] reads on the device clock, refresh times, and
    content-sized flushes (> 50 kpx)."""
    session.send("--redrawlog off")
    time.sleep(0.1)
    # Park the cursor at a known x first: mouse_read() clamps at the screen
    # edges and a clamped step reports no activity, which looked like a stall
    # in the first version of this scenario. Left edge, then 8 steps in, then
    # +-4 commands x 3 steps x 10 px stays well inside the 320 px width.
    session.send("--ball left 40")
    time.sleep(0.4)
    session.send("--ball right 8")
    time.sleep(0.4)
    snap0 = _instr_snapshot(session)
    idx = session.length()
    t0 = time.monotonic()
    k = 0
    while time.monotonic() - t0 < seconds:
        direction = "right" if (k // 4) % 2 == 0 else "left"
        session.send(f"--ball {direction} 3")
        k += 1
        # checkSerialCommand() drains ONE byte per main-loop iteration, so a
        # 60 ms cadence (267 B/s) overruns the reader as soon as the loop
        # slows to ~7 ms per iteration; 200 ms (80 B/s) keeps the serial path
        # out of the measurement.
        time.sleep(cadence_ms / 1000.0)
    time.sleep(0.5)
    recs = session.records_since(idx)
    usage_err = sum(1 for _, _, l in recs if "[BALL];err;usage" in l)
    snap1 = _instr_snapshot(session)
    instr = _instr_delta(snap0, snap1)
    ball_ms: List[int] = []
    loop_gaps: List[Dict[str, Any]] = []
    refr_ms: List[int] = []
    big_flush = 0
    sdmap_ms: List[int] = []
    for _, _, l in recs:
        m = re.search(r"\[ SDMAP \]\.\.\.Karte zusammengesetzt.*?, (\d+) ms \(", l)
        if m:
            sdmap_ms.append(int(m.group(1)))
            continue
        m = re.search(r"\[BALL\];x;(-?\d+);y;(-?\d+);btn;\d(?:;steps;\d+)?;ms;(\d+)", l)
        if m:
            ball_ms.append(int(m.group(3)))
            continue
        m = re.search(r"\[INSTR-LOOP\][; ]gap[; ]ms[; ](\d+)[; ]in[; ](\w+)", l)
        if m:
            loop_gaps.append({"ms": int(m.group(1)), "in": m.group(2)})
            continue
        m = re.search(r"\[REFR\];ms;\d+;px;(\d+);t_ms;(\d+)", l)
        if m:
            refr_ms.append(int(m.group(2)))
            if int(m.group(1)) > 50000:
                big_flush += 1
    ball_gaps = [ball_ms[i] - ball_ms[i - 1] for i in range(1, len(ball_ms))]
    stalls = [g for g in ball_gaps if g >= gap_ms]
    return {
        "sent_cmds": k,
        "ball_reads": len(ball_ms),
        "ball_gap_max_ms": max(ball_gaps) if ball_gaps else None,
        "ball_stalls": stalls,
        "loop_gaps_lvgl": [g["ms"] for g in loop_gaps if g["in"] == "lvgl"],
        "loop_gaps_other": [g for g in loop_gaps if g["in"] != "lvgl"],
        "refr_max_ms": max(refr_ms) if refr_ms else None,
        "big_flushes": big_flush,
        "sdmap_rebuilds_ms": sdmap_ms,
        "serial_garbled_cmds": usage_err,
        "loop_avg_us": instr["loop_avg_us"],
        "loop_n": instr["loop_n"],
        "sect_avg_us": instr["sect_avg_us"],
        "sect_share_pct": instr["sect_share_pct"],
    }


def scenario_msg_roll(session: TDeckSession, args: argparse.Namespace) -> Dict[str, Any]:
    """Operator report 2026-09-05: after new messages have arrived and the tab
    menu was collapsed once, rolling the trackball freezes the cursor for a
    few hundred ms every couple of seconds. The main loop does not stall on
    its own (no [INSTR-LOOP] gap in a plain roll); the stall only appears with
    that preparation. Sequence: roll (control), inject messages, drawer on
    and off, roll again. Verdict: the second roll must not show a main-loop
    gap in the lvgl section or a [BALL] read gap of --msgroll-gap-ms or more.
    A control phase that already stalls is reported as such (the instrument
    is then not discriminating on this build)."""
    gap_ms = args.msgroll_gap_ms
    idx_all = session.length()
    session.send("--tft on")
    session.send(f"--tab {args.msgroll_tab}")
    time.sleep(1.5)                      # tab 3 rebuilds the map tiles on entry
    session.send("--drawer off")
    time.sleep(0.3)
    # move the cursor away from the corner so the roll has room both ways
    session.send("--ball right 10")
    time.sleep(0.3)
    session.send("--ball down 10")
    time.sleep(0.4)
    uistat_before = get_uistat(session)

    control = _roll_phase(session, args.msgroll_seconds, gap_ms, args.msgroll_cadence_ms)

    injected = 0
    for i in range(args.msgroll_msgs):
        idx = session.send(f"--injectmsg {TEST_GROUP} msgroll {i}")
        if session.wait_for(r"\[INJECT\];ok", 2.0, since=idx) is not None:
            injected += 1
        time.sleep(0.6)
    time.sleep(1.0)
    drawer_on = _acked_step(session, "--drawer on", r"\[DRAWER\];1\b")
    time.sleep(0.5)
    drawer_off = _acked_step(session, "--drawer off", r"\[DRAWER\];0\b")
    time.sleep(0.5)

    after = _roll_phase(session, args.msgroll_seconds, gap_ms, args.msgroll_cadence_ms)
    uistat_after = get_uistat(session)

    # Diagnostic pass only when the stall is present: a short roll with the
    # redraw log on, and the invalidation backtraces that precede each loop
    # gap, symbolized -- that is the root-cause pointer, not just the verdict.
    diag: Optional[Dict[str, Any]] = None
    stalled = bool(after["loop_gaps_lvgl"]) or bool(after["ball_stalls"])
    if stalled:
        session.send("--redrawlog on")
        time.sleep(0.1)
        idx = session.length()
        t0 = time.monotonic()
        k = 0
        while time.monotonic() - t0 < min(6.0, args.msgroll_seconds):
            session.send(f"--ball {'right' if (k // 8) % 2 == 0 else 'left'} 3")
            k += 1
            time.sleep(0.06)
        time.sleep(0.5)
        session.send("--redrawlog off")
        time.sleep(0.2)
        recs = [l for _, _, l in session.records_since(idx)]
        gap_idx = [i for i, l in enumerate(recs) if re.search(r"\[INSTR-LOOP\][; ]gap", l)]
        bt_counts: Dict[Tuple[str, ...], int] = {}
        objs: Dict[str, int] = {}
        for g in gap_idx:
            for l in recs[max(0, g - 40):g]:
                m = re.search(r"\[REDRAW\];ms;\d+;obj;(0x[0-9a-f]+);cls;(\w+);area;(-?\d+;-?\d+;-?\d+;-?\d+);ra;0x[0-9a-f]+;bt;([0-9a-fx,]+)", l)
                if m:
                    key = tuple(m.group(4).split(","))
                    bt_counts[key] = bt_counts.get(key, 0) + 1
                    ok_ = f"{m.group(2)} {m.group(1)} area {m.group(3)}"
                    objs[ok_] = objs.get(ok_, 0) + 1
        top_bt = sorted(bt_counts.items(), key=lambda kv: -kv[1])[:5]
        sym_map = symbolize(sorted({a for key, _ in top_bt for a in key}), args.elf) if top_bt else {}
        diag = {
            "loop_gaps": len(gap_idx),
            "objects_before_gaps": sorted(objs.items(), key=lambda kv: -kv[1])[:6],
            "top_backtraces": [
                {"count": cnt, "frames": [_short_sym(sym_map.get(a, a)) for a in key]}
                for key, cnt in top_bt
            ],
        }

    sdmap_all = [int(m.group(1)) for _, _, l in session.records_since(idx_all)
                 for m in [re.search(r"\[ SDMAP \]\.\.\.Karte zusammengesetzt.*?, (\d+) ms \(", l)] if m]
    control_clean = not control["loop_gaps_lvgl"] and not control["ball_stalls"]
    slowdown = None
    if control.get("loop_avg_us") and after.get("loop_avg_us"):
        slowdown = round(after["loop_avg_us"] / control["loop_avg_us"], 2)
    loop_slowed = slowdown is not None and slowdown >= 1.5
    ok = control_clean and not stalled and not loop_slowed and injected == args.msgroll_msgs \
        and drawer_on["acked"] and drawer_off["acked"]
    return {
        "ok": ok,
        "control_clean": control_clean,
        "stalled_after": stalled,
        "loop_slowdown_x": slowdown,
        "loop_slowed": loop_slowed,
        "injected": injected,
        "drawer_on": drawer_on["acked"],
        "drawer_off": drawer_off["acked"],
        "control": control,
        "after": after,
        "diag": diag,
        "sdmap_rebuilds_total_ms": sdmap_all,
        "msg_list_before": uistat_before.get("msg_list") if uistat_before else None,
        "msg_list_after": uistat_after.get("msg_list") if uistat_after else None,
    }


# --------------------------------------------------------------------------
# map_tab_pick: TD-14 -- picking the map tab from the tab bar recomposes the
# SD map twice (bar-visible height, then bar-hidden height)
# --------------------------------------------------------------------------

# Map tab button in the tab bar: idx 3, spans x 120..160, bar y 27..78
# (measured on DK5EN-14 with --drawer on).
MAPTAB_BUTTON_X = (120, 160)
MAPTAB_BUTTON_Y = (27, 78)

_SDMAP_BUILT_RE = re.compile(
    r"Karte zusammengesetzt: zoom \d+, Kacheln \d+ \(fehlend \d+\), "
    r"(\d+)x(\d+) px, (\d+) ms \([^)]*\)(?:\s+from=(\w+))?"
)
_SDMAP_SKIPPED_RE = re.compile(
    r"Karte unveraendert, Aufbau uebersprungen(?:\s+from=(\w+))?"
)


def scenario_map_tab_pick(session: TDeckSession, args: argparse.Namespace) -> Dict[str, Any]:
    """TD-14 (docs/tdeck-map-double-rebuild-plan-20260905.md): picking the map
    tab from the tab bar with the trackball (as opposed to `--tab 3`, which
    switches directly) costs two full SD map recompositions back to back --
    LVGL bubbles the tab button matrix's VALUE_CHANGED up to the tabview
    (lv_tabview.c:233 LV_OBJ_FLAG_EVENT_BUBBLE), so tabview_event_cb() case 3
    runs once with the bar visible (294x140 px) and once after the bar
    collapsed (294x182 px). Both block the main loop (~1.3 s combined [INSTR-LOOP] gap
    in the lvgl section); cursor and touch freeze for the duration.

    Drives the trackball onto the map tab's bar button (idx 3, x 120..160,
    y 27..78) and clicks it -- reproducing the operator's actual gesture,
    not `--tab 3` which bypasses the bar entirely -- then asserts against a
    reference height taken from a plain `--tab 2` / `--tab 3` switch (bar
    already hidden, so only the bar-hidden rebuild is possible there).
    PASS iff: `[TAB];active;3` after the click, exactly one
    `Karte zusammengesetzt` line since the click, that line's `from` tag is
    `tab`, and its height equals the reference height.

    Baseline on the unfixed build (as of 2026-09-05): FAILs with exactly two
    rebuilds, 294x140 then 294x182, and a ~1.3 s lvgl loop gap. Once the fix
    lands the bubbled duplicate is dropped and the bar is hidden before
    sdmap_refresh() is called, so only one 294x182 rebuild remains, matching
    the reference height (measured 2026-09-05: 718 ms lvgl gap).
    """
    session.send("--tft on")
    time.sleep(0.2)
    session.send("--tab 0")
    time.sleep(1.5)
    session.send("--drawer on")
    session.wait_for(r"\[DRAWER\];1\b", 2.0)
    time.sleep(0.5)
    session.send("--ball left 40")
    time.sleep(0.5)
    session.send("--ball up 40")  # parks at x=0, y=0 (BALL);x;0;y;0
    time.sleep(0.5)
    session.send("--ball right 8")  # BALL_MAX_STEPS_PER_READ=8, one read = 8*10 px
    time.sleep(0.5)
    idx_pos = session.length()
    session.send("--ball right 6")  # -> x=140 (button spans 120..160)
    time.sleep(0.5)
    session.send("--ball down 5")  # -> y=50 (bar spans y 27..78)
    time.sleep(0.5)

    x = y = None
    for _, _, l in session.records_since(idx_pos):
        mm = re.search(r"\[BALL\];x;(-?\d+);y;(-?\d+)", l)
        if mm:
            x, y = int(mm.group(1)), int(mm.group(2))
    cursor_ok = (
        x is not None
        and y is not None
        and MAPTAB_BUTTON_X[0] <= x < MAPTAB_BUTTON_X[1]
        and MAPTAB_BUTTON_Y[0] <= y < MAPTAB_BUTTON_Y[1]
    )
    if not cursor_ok:
        return {
            "ok": False,
            "reason": f"cursor_missed: x={x} y={y}, expected "
            f"{MAPTAB_BUTTON_X[0]}<=x<{MAPTAB_BUTTON_X[1]}, "
            f"{MAPTAB_BUTTON_Y[0]}<=y<{MAPTAB_BUTTON_Y[1]}",
            "cursor": {"x": x, "y": y},
        }

    click_idx = session.length()
    session.send("--ball click 1")
    post_click_lines = session.collect(args.maptab_settle_seconds, since=click_idx)

    tlist_idx = session.send("--tab list")
    m_active = session.wait_for(r"\[TAB\];active;(\d+)", 2.0, since=tlist_idx)
    tab_active = int(m_active.group(1)) if m_active else None
    time.sleep(0.3)

    rebuilds: List[Dict[str, Any]] = []
    sdmap_skipped = 0
    for l in post_click_lines:
        m = _SDMAP_BUILT_RE.search(l)
        if m:
            rebuilds.append(
                {
                    "w": int(m.group(1)),
                    "h": int(m.group(2)),
                    "ms": int(m.group(3)),
                    "from": m.group(4),
                }
            )
            continue
        if _SDMAP_SKIPPED_RE.search(l):
            sdmap_skipped += 1

    loop_gaps_lvgl = [
        int(m.group(1))
        for l in post_click_lines
        for m in [re.search(r"\[INSTR-LOOP\][; ]gap[; ]ms[; ](\d+)[; ]in[; ]lvgl", l)]
        if m
    ]

    # Reference height: a plain tab switch with the bar already hidden, so
    # only the bar-hidden rebuild is reachable on that path.
    session.send("--tab 2")
    session.collect(1.5)
    ref_idx = session.length()
    session.send("--tab 3")
    ref_lines = session.collect(3.0, since=ref_idx)
    ref_h = None
    for l in ref_lines:
        m = _SDMAP_BUILT_RE.search(l)
        if m:
            ref_h = int(m.group(2))

    reason = None
    if tab_active != 3:
        reason = f"[TAB];active;{tab_active} after the click, expected 3"
    elif len(rebuilds) != 1:
        reason = f"expected exactly one 'Karte zusammengesetzt' line since the click, got {len(rebuilds)}"
    elif rebuilds[0]["from"] != "tab":
        reason = f"rebuild from tag is {rebuilds[0]['from']!r}, expected 'tab'"
    elif ref_h is not None and rebuilds[0]["h"] != ref_h:
        reason = f"rebuild height {rebuilds[0]['h']} != reference height {ref_h}"

    ok = reason is None
    return {
        "ok": ok,
        "reason": reason,
        "cursor": {"x": x, "y": y},
        "tab_active": tab_active,
        "sdmap_rebuilds": rebuilds,
        "sdmap_skipped": sdmap_skipped,
        "ref_h": ref_h,
        "loop_gaps_lvgl": loop_gaps_lvgl,
    }


def scenario_heap(session: TDeckSession, args: argparse.Namespace) -> Dict[str, Any]:
    idx0 = session.send("--heap h0")
    m0 = session.wait_for(r"\[INSTR-HEAP\];h0;", 2.0, since=idx0)
    h0 = parse_line(m0.string) if m0 else None

    for i in range(args.heap_count):
        t_start = time.monotonic()
        session.send(f"--injectmsg {TEST_GROUP} heap probe {i}")
        elapsed = time.monotonic() - t_start
        remaining = 3.0 - elapsed
        if remaining > 0:
            time.sleep(remaining)

    idx1 = session.send("--heap h1")
    m1 = session.wait_for(r"\[INSTR-HEAP\];h1;", 2.0, since=idx1)
    h1 = parse_line(m1.string) if m1 else None
    session.send("--instr")
    session.collect(1.0)

    delta = heap_delta(h0, h1)
    ok = h0 is not None and h1 is not None
    return {"ok": ok, "h0": h0, "h1": h1, "delta": delta}


MSG_TAB_MAX_MESSAGES = 50  # mirrors lv_obj_functions.cpp; the view must never hold more


def scenario_trim(session: TDeckSession, args: argparse.Namespace) -> Dict[str, Any]:
    """TD-03 / H1 regression: the rendered list of the active tab is capped.

    Injects --trim-count messages into one group while that group's tab is the
    active one (the fast path in msg_tabs_add_message appends to the view
    without re-rendering), samples [UISTAT] msg_list after every message and
    fails if the child count ever exceeds MSG_TAB_MAX_MESSAGES. Heap is sampled
    before and after so the PSRAM cost per message is visible in the report.
    """
    session.send("--tab 0")
    time.sleep(0.3)
    idx0 = session.send("--heap h0")
    m0 = session.wait_for(r"\[INSTR-HEAP\];h0;", 2.0, since=idx0)
    h0 = parse_line(m0.string) if m0 else None

    samples: List[Optional[int]] = []
    inject_fail = 0
    for i in range(args.trim_count):
        idx = session.send(f"--injectmsg {TEST_GROUP} trim probe {i}")
        if session.wait_for(r"\[INJECT\];ok", 2.0, since=idx) is None:
            inject_fail += 1
        time.sleep(0.2)
        st = get_uistat(session)
        if st is None:  # one probe miss is a serial artefact, not a finding
            st = get_uistat(session)
        samples.append(st.get("msg_list") if st else None)

    idx1 = session.send("--heap h1")
    m1 = session.wait_for(r"\[INSTR-HEAP\];h1;", 2.0, since=idx1)
    h1 = parse_line(m1.string) if m1 else None

    seen = [v for v in samples if v is not None]
    max_children = max(seen) if seen else None
    final_children = seen[-1] if seen else None
    expected_final = min(args.trim_count, MSG_TAB_MAX_MESSAGES)
    missed = args.trim_count - len(seen)
    ok = (
        inject_fail == 0
        and samples
        and samples[-1] is not None
        and missed <= args.trim_count // 10
        and max_children is not None
        and max_children <= MSG_TAB_MAX_MESSAGES
        and final_children == expected_final
    )
    return {
        "ok": ok,
        "count": args.trim_count,
        "inject_fail": inject_fail,
        "probe_missed": missed,
        "max_children": max_children,
        "final_children": final_children,
        "expected_final": expected_final,
        "limit": MSG_TAB_MAX_MESSAGES,
        "samples": samples,
        "h0": h0,
        "h1": h1,
        "delta": heap_delta(h0, h1),
    }


def _top_backtraces(
    records: Iterable[Optional[Dict[str, Any]]], elf: str, limit: int = 8
) -> List[Dict[str, Any]]:
    """Group REDRAW;obj records by full backtrace, symbolized, most-common first.

    Shared by scenario_inject-style reporting and scenario_sleep.
    """
    bt_counts: Dict[Tuple[str, ...], int] = {}
    for rec in records:
        if rec and rec.get("kind") == "REDRAW" and rec.get("variant") == "obj":
            key = tuple(rec.get("bt") or ())
            bt_counts[key] = bt_counts.get(key, 0) + 1
    top_bt = sorted(bt_counts.items(), key=lambda kv: -kv[1])[:limit]
    addrs = sorted({a for key, _ in top_bt for a in key})
    sym_map = symbolize(addrs, elf) if addrs else {}
    return [
        {"count": cnt, "frames": [_short_sym(sym_map.get(a, a)) for a in key]}
        for key, cnt in top_bt
    ]


def scenario_boot(session: TDeckSession, args: argparse.Namespace) -> Dict[str, Any]:
    """H-R3 support scenario: inspect what the device logged during its own boot.

    Sends no commands -- purely inspects the lines already recorded since the
    session opened (index 0), i.e. everything from CLIENT STARTED onward.
    """
    raw_lines = [l for _, _, l in session.records_since(0)]
    parsed = [parse_line(l) for l in raw_lines]

    boot_msgs = [r["text"] for r in parsed if r and r.get("kind") == "BOOT" and r.get("variant") == "msg"]
    audio_recs = [r for r in parsed if r and r.get("kind") == "BOOT" and r.get("variant") == "audio"]
    audio_outcome = audio_recs[-1] if audio_recs else None
    init_recs = [r for r in parsed if r and r.get("kind") == "BOOT" and r.get("variant") == "init"]
    init_summary = init_recs[-1] if init_recs else None
    # Pre-existing freeform "[INIT]...text" debug lines (sensor/subsystem init
    # logging predating this instrumentation) -- not a parsed [BOOT];init
    # record, just raw text worth keeping around for a human to skim.
    init_log_lines = [l for l in raw_lines if "[INIT]" in l]
    fail_lines = [l for l in raw_lines if "FAIL" in l]

    ok = init_summary is not None and not fail_lines
    return {
        "ok": ok,
        "boot_msgs": boot_msgs,
        "audio_outcome": audio_outcome,
        "init_summary": init_summary,
        "init_log_lines": init_log_lines,
        "fail_lines": fail_lines,
    }


def scenario_uptime(session: TDeckSession, args: argparse.Namespace) -> Dict[str, Any]:
    """TM-30 / upstream #1083: touch (input) latency that grows with uptime.
    Every --uptime-step seconds: a trackball input probe (event -> repaint
    latency from the host timestamps of [BALL] and the next [REFR]), --uistat,
    --heap and --instr (loop max, per-section max, gap lines) -- one row per
    step. The verdict is the trend: fails if the last row's input latency or
    loop max exceeds --uptime-growth x the first row's, or on a crash marker."""
    rows: List[Dict[str, Any]] = []
    t_start = time.monotonic()
    session.send("--tab 0")
    time.sleep(0.5)
    for step in range(args.uptime_steps):
        idx0 = session.send("--instreset")
        session.wait_for(r"\[INSTR\][; ]reset", 2.0, since=idx0)
        remaining = args.uptime_step
        while remaining > 0:
            chunk = min(remaining, 30.0)
            session.collect(chunk)
            remaining -= chunk
        session.send("--tft on")
        time.sleep(0.3)
        session.send("--redrawlog on")
        time.sleep(0.2)
        lat_ms: List[float] = []
        for d in ("down", "up"):
            idx = session.send(f"--ball {d} 5")
            session.wait_for(r"\[REFR\]", 2.0, since=idx)
            recs = session.records_since(idx)
            t_b = next((t for t, _, l in recs if "[BALL]" in l), None)
            t_r = next((t for t, _, l in recs if "[REFR]" in l and t_b is not None and t >= t_b), None)
            if t_b is not None and t_r is not None:
                lat_ms.append((t_r - t_b) * 1000.0)
            time.sleep(0.5)
        session.send("--redrawlog off")
        time.sleep(0.2)
        st = get_uistat(session)
        idxh = session.send("--heap up")
        mh = session.wait_for(r"\[INSTR-HEAP\];up;", 2.0, since=idxh)
        heap = parse_line(mh.string) if mh else None
        idxi = session.send("--instr")
        session.wait_for(r"\[INSTR-GUI\]|\[INSTR-GAPS\]", 3.0, since=idxi)
        session.collect(0.5)
        lines = [l for _, _, l in session.records_since(idxi)]
        loop = next((l for l in lines if "[INSTR-LOOP]" in l and "gap" not in l), None)
        mloop = re.search(r"max_us[; ](\d+)", loop or "")
        sects: Dict[str, int] = {}
        for l in lines:
            ms = re.search(r"\[INSTR-SECT\][; ](\w+).*max_us[; ](\d+)", l)
            if ms:
                sects[ms.group(1)] = int(ms.group(2))
        gaps = [l.strip()[:100] for _, _, l in session.records_since(idx0) if "[INSTR-LOOP]" in l and "gap" in l]
        crash = [l for _, _, l in session.records_since(idx0) if re.search(CRASH_PATTERN, l)]
        rows.append({
            "step": step,
            "uptime_s": round(time.monotonic() - t_start),
            "input_lat_ms": round(max(lat_ms), 1) if lat_ms else None,
            "input_probes": len(lat_ms),
            "loop_max_us": int(mloop.group(1)) if mloop else None,
            "worst_section": max(sects.items(), key=lambda kv: kv[1])[0] if sects else None,
            "worst_section_us": max(sects.values()) if sects else None,
            "gaps": gaps[:5],
            "msg_list": st.get("msg_list") if st else None,
            "int_free": heap.get("int_free") if heap else None,
            "psram_free": heap.get("psram_free") if heap else None,
            "crash": crash[:3],
        })
        if crash:
            break
    first, last = rows[0], rows[-1]

    def grew(key: str) -> Optional[bool]:
        a, b = first.get(key), last.get(key)
        if a is None or b is None or a == 0:
            return None
        return b > a * args.uptime_growth

    ok = (
        len(rows) == args.uptime_steps
        and not last["crash"]
        and grew("input_lat_ms") is not True
        and grew("loop_max_us") is not True
    )
    return {"ok": ok, "rows": rows, "steps": args.uptime_steps, "step_s": args.uptime_step,
            "input_lat_grew": grew("input_lat_ms"), "loop_max_grew": grew("loop_max_us")}


def scenario_displaycmd(session: TDeckSession, args: argparse.Namespace) -> Dict[str, Any]:
    """TM-33 (b) / upstream #690: the user command --display off/on must drive
    the TFT (it used to be the U8g2 path only, a no-op on the T-Deck).
    --display off -> [TFT];off and --tft state reports sleeping;1;
    --display on -> [TFT];on and sleeping;0. Runs twice. --display on is sent
    last so the persisted display flag is left cleared."""
    def state() -> Optional[int]:
        idx = session.send("--tft state")
        m = session.wait_for(r"\[TFT\];sleeping;(\d)", 2.0, since=idx)
        return int(m.group(1)) if m else None

    session.send("--tft on")
    time.sleep(0.5)
    steps = []
    for _ in range(2):
        for cmd, marker, want in (("--display off", r"\[TFT\];off", 1), ("--display on", r"\[TFT\];on", 0)):
            idx = session.send(cmd)
            seen = session.wait_for(marker, 3.0, since=idx) is not None
            time.sleep(0.8)
            st = state()
            steps.append({"cmd": cmd, "marker_seen": seen, "sleeping": st, "want": want, "ok": seen and st == want})
    ok = all(s["ok"] for s in steps)
    return {"ok": ok, "steps": steps}


def scenario_screen(session: TDeckSession, args: argparse.Namespace) -> Dict[str, Any]:
    """Sanity-check --screencrc readback stability, and that the map tab renders content."""
    crc_a = get_screencrc(session)
    time.sleep(1.0)
    crc_b = get_screencrc(session)
    readback_stable = bool(crc_a and crc_b and crc_a.get("crc") == crc_b.get("crc"))

    session.send("--tab 3")
    time.sleep(0.5)
    map_crc = get_screencrc(session)
    map_has_content = bool(map_crc and map_crc.get("nonblack", 0) > 1000)

    return {
        "ok": readback_stable and map_has_content,
        "crc_a": crc_a,
        "crc_b": crc_b,
        "readback_stable": readback_stable,
        "map_crc": map_crc,
        "map_has_content": map_has_content,
    }


def scenario_sleep(session: TDeckSession, args: argparse.Namespace) -> Dict[str, Any]:
    """H-R3 core hypothesis test: after display wake, is the screen repainted?

    S0 = screen content while awake; S1 = screen content right after wake
    (with an inject fired while asleep to give the device something to want
    to redraw); S2 = screen content after a forced full repaint (drawer
    open/close). If S1 == S2 the post-wake screen already matches what a
    full repaint produces -- the wake path is fine. If S1 != S2 the device
    is showing stale content after wake and only a forced repaint fixes it.
    """
    # a. establish a known-good baseline while the display is awake.
    session.send("--tab 0")
    time.sleep(0.3)
    tft_on_idx = session.send("--tft on")
    session.wait_for(r"\[TFT\]", 2.0, since=tft_on_idx)
    s0 = get_screencrc(session)

    # b. put the display to sleep.
    off_idx = session.send("--tft off")
    tft_off_acked = session.wait_for(r"\[TFT\];sleeping;1\b", 2.0, since=off_idx) is not None

    # c. while asleep, inject a message and see whether anything tries to redraw.
    session.send("--redrawlog on")
    time.sleep(0.1)
    inject_idx = session.send(f"--injectmsg {TEST_GROUP} sleep probe")
    inject_ok = session.wait_for(r"\[INJECT\];ok", 2.0, since=inject_idx) is not None
    asleep_lines = session.collect(1.5, since=inject_idx)
    asleep_parsed = [parse_line(l) for l in asleep_lines]
    refr_px_while_asleep = [r["px"] for r in asleep_parsed if r and r.get("kind") == "REFR"]

    # d. wake the display and observe the repaint (or lack of one).
    wake_idx = session.send("--tft on")
    tft_on_acked = session.wait_for(r"\[TFT\];sleeping;0\b", 2.0, since=wake_idx) is not None
    wake_lines = session.collect(1.5, since=wake_idx)
    session.send("--redrawlog off")
    time.sleep(0.1)

    wake_parsed = [parse_line(l) for l in wake_lines]
    refr_after_wake = [r for r in wake_parsed if r and r.get("kind") == "REFR"]
    full_refresh_after_wake = any(r["px"] == 76800 for r in refr_after_wake)
    max_px_after_wake = max((r["px"] for r in refr_after_wake), default=0)
    redraw_after_wake = [
        r for r in wake_parsed if r and r.get("kind") == "REDRAW" and r.get("variant") == "obj"
    ]
    redraw_count_after_wake = len(redraw_after_wake)
    top_backtraces_after_wake = _top_backtraces(redraw_after_wake, args.elf)

    # e. S1, then force a known-good repaint (drawer open/close) and take S2.
    s1 = get_screencrc(session)
    drawer_on_idx = session.send("--drawer on")
    session.wait_for(r"\[DRAWER\];1\b", 1.0, since=drawer_on_idx)
    drawer_off_idx = session.send("--drawer off")
    session.wait_for(r"\[DRAWER\];0\b", 1.0, since=drawer_off_idx)
    session.collect(1.0)
    s2 = get_screencrc(session)

    # f. verdict.
    screen_changed_by_wake = bool(s0 and s1 and s0.get("crc") != s1.get("crc"))
    wake_matches_forced_repaint = bool(s1 and s2 and s1.get("crc") == s2.get("crc"))
    ok = wake_matches_forced_repaint

    return {
        "ok": ok,
        "s0": s0,
        "s1": s1,
        "s2": s2,
        "tft_off_acked": tft_off_acked,
        "tft_on_acked": tft_on_acked,
        "inject_ok": inject_ok,
        "refr_px_while_asleep": refr_px_while_asleep,
        "screen_changed_by_wake": screen_changed_by_wake,
        "wake_matches_forced_repaint": wake_matches_forced_repaint,
        "full_refresh_after_wake": full_refresh_after_wake,
        "max_px_after_wake": max_px_after_wake,
        "redraw_count_after_wake": redraw_count_after_wake,
        "top_backtraces_after_wake": top_backtraces_after_wake,
    }


def scenario_disptest(session: TDeckSession, args: argparse.Namespace) -> Dict[str, Any]:
    """TM-41: colour/geometry sequence, asserted on the push path.

    --screencrc is void on this panel (MISO not driven), so the device cannot
    be asked what the glass shows. --disptest instead CRC32s exactly the byte
    block it hands to tft.pushColors() for every frame and prints it; this
    scenario re-renders the same frames with tdeck_parse.disptest_* (the same
    integer rasterisers) and compares CRC by CRC. A pass means the right
    pixels reached the panel; whether they light up is for an operator's eye.
    """
    phase = args.disptest_phase
    stride = max(1, args.disptest_stride)

    expected = disptest_expected(phase, stride)
    idx = session.send(f"--disptest {phase} {stride}")

    begin = session.wait_for(r"\[DISPTEST\];(begin|err)", 15.0, since=idx)
    if begin is None:
        return {"ok": False, "reason": "no [DISPTEST];begin -- firmware without --disptest?"}
    begin_rec = parse_line(begin.string)
    if begin_rec and begin_rec.get("variant") == "err":
        return {"ok": False, "reason": f"device refused: {begin_rec.get('reason')}", "begin": begin_rec}

    # 250 ms/frame is ~6x the measured cost and still bounds the wait.
    timeout = 30.0 + 0.25 * len(expected)
    t0 = time.monotonic()
    end = session.wait_for(r"\[DISPTEST\];end", timeout, since=idx)
    wall_s = time.monotonic() - t0
    lines = session.records_since(idx)
    steps = [
        r
        for r in (parse_line(l) for _, _, l in lines)
        if r and r.get("kind") == "DISPTEST" and r.get("variant") == "step"
    ]
    end_rec = parse_line(end.string) if end else None

    mismatches: List[Dict[str, Any]] = []
    for i, (want_phase, want_n, want_crc) in enumerate(expected):
        got = steps[i] if i < len(steps) else None
        if got is None:
            mismatches.append({"i": i, "phase": want_phase, "n": want_n, "why": "missing"})
            continue
        if got["phase"] != want_phase or got["n"] != want_n:
            mismatches.append(
                {
                    "i": i,
                    "phase": want_phase,
                    "n": want_n,
                    "why": "out of order",
                    "got": f"{got['phase']}/{got['n']}",
                }
            )
            continue
        if got["crc"] != want_crc:
            mismatches.append(
                {
                    "i": i,
                    "phase": want_phase,
                    "n": want_n,
                    "why": "crc",
                    "want": f"{want_crc:08x}",
                    "got": f"{got['crc']:08x}",
                }
            )

    step_ms = sorted(r["ms"] for r in steps)
    device_ms = end_rec["ms"] if end_rec else None
    fps = (len(steps) / (device_ms / 1000.0)) if device_ms else None
    wrong_px = sorted({r["px"] for r in steps if r["px"] != 320 * 240})

    return {
        "ok": bool(end_rec) and not mismatches and len(steps) == len(expected),
        "phase": phase,
        "stride": stride,
        "steps_expected": len(expected),
        "steps_seen": len(steps),
        "steps_ok": len(expected) - len(mismatches),
        "mismatches": mismatches[:10],
        "mismatch_count": len(mismatches),
        "first_mismatch": mismatches[0] if mismatches else None,
        "begin": begin_rec,
        "end": end_rec,
        "device_ms": device_ms,
        "wall_s": round(wall_s, 1),
        "fps": round(fps, 1) if fps else None,
        "step_ms_min": step_ms[0] if step_ms else None,
        "step_ms_p50": step_ms[len(step_ms) // 2] if step_ms else None,
        "step_ms_max": step_ms[-1] if step_ms else None,
        "wrong_px": wrong_px,
    }


# --------------------------------------------------------------------------
# gps_experiment: TM-14 -- does GPS fix processing load the main loop?
# --------------------------------------------------------------------------


def _gps_state(session: TDeckSession, timeout: float = 4.0) -> Optional[bool]:
    """--pos also prints '...GPS: on/off' (bGPSON); used to confirm --gps on/off took."""
    idx = session.send("--pos")
    if session.wait_for(r"GPS:", timeout, since=idx) is None:
        return None
    for _, _, l in session.records_since(idx):
        m = re.search(r"GPS:\s*(on|off)", l)
        if m:
            return m.group(1) == "on"
    return None


def _loop_max_stats(max_values: Sequence[Optional[float]]) -> Dict[str, Any]:
    vals = [v for v in max_values if v is not None]
    return {
        "samples": len(vals),
        "p50_us": _pct(vals, 0.5),
        "p90_us": _pct(vals, 0.9),
        "p99_us": _pct(vals, 0.99),
        "max_us": max(vals) if vals else None,
        "over_50ms_count": sum(1 for v in vals if v > 50000),
        "over_100ms_count": sum(1 for v in vals if v > 100000),
    }


def _gps_window_capture(
    session: TDeckSession, window_seconds: float, load_count: int, label: str
) -> Dict[str, Any]:
    """Apply `load_count` --injectmsg messages (scenario_inject's own
    TEST_GROUP / --injectmsg / "[INJECT];ok" pattern) evenly across
    window_seconds, sampling [INSTR-LOOP] max_us in the sub-window before
    each one via --instreset/--instr. The console has no per-iteration loop
    trace, so this sub-window sampling is the finest granularity available;
    the percentiles below are over those sub-window maxima, not over
    individual loop() calls.
    """
    n = max(1, load_count)
    sub = max(1.0, window_seconds / n)
    samples: List[Dict[str, Any]] = []
    inject_ok = 0
    for i in range(n):
        ridx = session.send("--instreset")
        session.wait_for(r"\[INSTR\][; ]reset", 2.0, since=ridx)
        iidx = session.send(f"--injectmsg {TEST_GROUP} {label} {i}")
        if session.wait_for(r"\[INJECT\];ok", 2.0, since=iidx) is not None:
            inject_ok += 1
        settle = 1.0 if sub > 1.0 else 0.0
        session.collect(max(0.0, sub - settle))
        instr_idx = session.send("--instr")
        m = session.wait_for(r"\[INSTR-LOOP\];", 2.0, since=instr_idx)
        rec = parse_line(m.string) if m else None
        samples.append(
            {
                "i": i,
                "max_us": rec.get("max_us") if rec else None,
                "avg_us": rec.get("avg_us") if rec else None,
                "n": rec.get("n") if rec else None,
            }
        )
    stats = _loop_max_stats([s["max_us"] for s in samples])
    return {"inject_ok": inject_ok, "inject_total": n, "samples": samples, "stats": stats}


def scenario_gps_experiment(session: TDeckSession, args: argparse.Namespace) -> Dict[str, Any]:
    """TM-14 A/B experiment: does GPS fix processing load the main loop?

    Phase A: GPS on, a fixed injected message load spread across
    --gps-window seconds (reusing scenario_inject's --injectmsg load
    generator via args.inject_count), sampling [INSTR-LOOP] sub-window
    maxima throughout. Phase B: --gps off, settle 10 s, then an identical
    load + window. GPS is restored to on afterward regardless of outcome.
    Reports p50/p90/p99/max and >50ms/>100ms tail counts per phase, plus a
    plain verdict line comparing the tails.
    """
    session.send("--tab 0")
    time.sleep(0.3)

    session.send("--gps on")
    time.sleep(0.5)
    gps_on_confirmed = _gps_state(session)

    phase_on = _gps_window_capture(session, args.gps_window, args.inject_count, "gpsA")

    session.send("--gps off")
    time.sleep(0.5)
    gps_off_confirmed = _gps_state(session)
    time.sleep(10.0)  # settle before phase B, per the TM-14 procedure

    phase_off = _gps_window_capture(session, args.gps_window, args.inject_count, "gpsB")

    session.send("--gps on")
    time.sleep(0.5)
    gps_restored_confirmed = _gps_state(session)

    p99_on, p99_off = phase_on["stats"]["p99_us"], phase_off["stats"]["p99_us"]
    tail_on, tail_off = phase_on["stats"]["over_100ms_count"], phase_off["stats"]["over_100ms_count"]
    if p99_on is None or p99_off is None:
        verdict = "inconclusive -- missing [INSTR-LOOP] samples in one or both phases"
    elif p99_on > p99_off * 1.2 or tail_on > tail_off:
        verdict = (
            f"GPS ON tail is worse than GPS OFF (p99 {p99_on:.0f}us vs {p99_off:.0f}us, "
            f">100ms count {tail_on} vs {tail_off}) -- GPS processing measurably loads the loop"
        )
    elif p99_off > p99_on * 1.2 or tail_off > tail_on:
        verdict = (
            f"GPS OFF tail is worse than GPS ON (p99 {p99_off:.0f}us vs {p99_on:.0f}us) -- "
            f"unexpected direction, treat as noise/harness artefact rather than a GPS finding"
        )
    else:
        verdict = "no clear difference between GPS on/off loop tails -- GPS processing does not measurably load the loop"

    # gps_off_confirmed is the reported *state* (True = GPS still on), not a
    # "did the confirmation succeed" flag -- only fail on the one state value
    # that means the --gps off command visibly did not take (True); False
    # (confirmed off, as wanted) and None (board doesn't report GPS state)
    # are both fine.
    ok = p99_on is not None and p99_off is not None and gps_off_confirmed is not True

    return {
        "ok": ok,
        "window_seconds": args.gps_window,
        "load_count": args.inject_count,
        "gps_on_confirmed": gps_on_confirmed,
        "gps_off_confirmed": gps_off_confirmed,
        "gps_restored_confirmed": gps_restored_confirmed,
        "phase_gps_on": phase_on,
        "phase_gps_off": phase_off,
        "verdict": verdict,
    }


# --------------------------------------------------------------------------
# flush_lora_correlation: TM-06 (c) -- lost/suspect flushes vs LoRa SPI use
# --------------------------------------------------------------------------


def _force_redraws(session: TDeckSession, cycles: int, pause: float = 0.3) -> None:
    """Drawer open/close is the harness's existing forced-full-repaint trick
    (see scenario_sleep step e / _acked_step's --drawer usage); reused here
    purely to keep TFT flushes happening throughout an observation window."""
    for _ in range(cycles):
        session.send("--drawer on")
        time.sleep(pause)
        session.send("--drawer off")
        time.sleep(pause)


def _flush_lora_observe(
    session: TDeckSession, window_seconds: float, with_lora: bool, args: argparse.Namespace, label: str
) -> Dict[str, Any]:
    session.send("--spitrace on")
    time.sleep(0.2)
    reset_idx = session.send("--instreset")
    session.wait_for(r"\[INSTR\][; ]reset", 2.0, since=reset_idx)
    start = session.length()

    lora_started: Optional[bool] = None
    lora_done: Optional[bool] = None
    if with_lora:
        lidx = session.send(f"--loratx {args.loratx_count} {args.loratx_interval_ms}")
        lora_started = session.wait_for(r"\[INJ\];loratx;start;", 3.0, since=lidx) is not None

    cycles = max(1, int(window_seconds / (2 * args.flush_force_pause)))
    _force_redraws(session, cycles, args.flush_force_pause)
    remaining = window_seconds - cycles * 2 * args.flush_force_pause
    if remaining > 0:
        session.collect(remaining)

    if with_lora:
        lora_done = session.wait_for(r"\[INJ\];loratx;done;", 3.0, since=start) is not None

    end = session.length()
    session.send("--spitrace off")
    time.sleep(0.2)
    instr_idx = session.send("--instr")
    m = session.wait_for(r"\[INSTR-FLUSH\];", 2.0, since=instr_idx)
    flush_instr = parse_line(m.string) if m else None

    recs = session.records_range(start, end)
    spitrace: List[Dict[str, Any]] = []
    lora_events: List[Tuple[float, str]] = []
    for _, t_mono, l in recs:
        rec = parse_line(l)
        if rec and rec.get("kind") == "SPITRACE" and rec.get("variant") == "flush":
            spitrace.append({**rec, "t_mono": t_mono})
        elif rec and rec.get("kind") == "INJ" and rec.get("variant") == "loratx_q":
            lora_events.append((t_mono, rec.get("id")))

    window_start_t = recs[0][1] if recs else time.monotonic()
    rows = []
    for i, sp in enumerate(spitrace):
        t0 = spitrace[i - 1]["t_mono"] if i > 0 else window_start_t
        t1 = sp["t_mono"]
        lora_between = [lid for (t, lid) in lora_events if t0 <= t <= t1]
        l_users = sp["users"].get("L", 0)
        chg = sp.get("chg") or []
        rows.append(
            {
                "seq": sp.get("seq"),
                "users": sp.get("users"),
                "l_users": l_users,
                "chg": chg,
                "lora_between": lora_between,
                "lora_activity": bool(lora_between) or l_users > 0,
            }
        )

    with_l = [r for r in rows if r["lora_activity"]]
    without_l = [r for r in rows if not r["lora_activity"]]

    return {
        "label": label,
        "with_lora": with_lora,
        "lora_started": lora_started,
        "lora_done": lora_done,
        "flush_instr": flush_instr,
        "flush_count": len(rows),
        "rows": rows,
        "lora_activity_flushes": len(with_l),
        "lora_activity_with_chg": sum(1 for r in with_l if r["chg"]),
        "no_lora_activity_flushes": len(without_l),
        "no_lora_activity_with_chg": sum(1 for r in without_l if r["chg"]),
    }


def scenario_flush_lora_correlation(session: TDeckSession, args: argparse.Namespace) -> Dict[str, Any]:
    """TM-06 (c): correlate TFT flush activity ([SPITRACE]) with LoRa SPI bus
    traffic (--loratx) to see whether flushes that coincide with LoRa
    activity are more often the ones with a changed register snapshot
    (chg != none) than flushes with no LoRa activity nearby -- i.e. whether
    LoRa bus sharing is visibly perturbing the panel's SPI state during a
    flush. Runs once with --loratx running against forced redraws, once as a
    control with the same forced redraws and no LoRa traffic.
    """
    session.send("--tab 0")
    time.sleep(0.3)

    lora_phase = _flush_lora_observe(session, args.flush_window, True, args, "loratx")
    time.sleep(1.0)
    control_phase = _flush_lora_observe(session, args.flush_window, False, args, "control")

    def _rate(n: int, d: int) -> Optional[float]:
        return round(n / d, 2) if d else None

    lora_rate = _rate(lora_phase["lora_activity_with_chg"], lora_phase["lora_activity_flushes"])
    control_rate = _rate(control_phase["lora_activity_with_chg"], control_phase["lora_activity_flushes"])

    if lora_phase["flush_count"] == 0:
        conclusion = "inconclusive -- no [SPITRACE] flush lines captured"
    elif lora_phase["lora_activity_with_chg"] > 0 and lora_rate is not None and (
        control_rate is None or lora_rate > control_rate
    ):
        conclusion = (
            f"LoRa SPI activity coincides with register changes on "
            f"{lora_phase['lora_activity_with_chg']}/{lora_phase['lora_activity_flushes']} flushes "
            f"during --loratx vs {control_phase['lora_activity_with_chg']}/{control_phase['lora_activity_flushes']} "
            f"in the control window -- suspect flushes correlate with LoRa bus sharing"
        )
    else:
        conclusion = "no clear correlation between LoRa SPI activity and TFT register changes in this run"

    ok = lora_phase["lora_started"] is not False and lora_phase["flush_count"] > 0

    return {
        "ok": ok,
        "window_seconds": args.flush_window,
        "loratx": {"n": args.loratx_count, "interval_ms": args.loratx_interval_ms},
        "lora_phase": lora_phase,
        "control_phase": control_phase,
        "conclusion": conclusion,
    }


# --------------------------------------------------------------------------
# touch_inject: TM-19 -- injected touch path sanity
# --------------------------------------------------------------------------

# 320x240 panel; centre and two off-centre points well away from the corners
# where the drawer hamburger / status icons live, so a tap can't trigger
# navigation and turn this into something other than an injection-path test.
TOUCH_TAP_POINTS = [(160, 120), (160, 60), (80, 180)]


def scenario_touch_inject(session: TDeckSession, args: argparse.Namespace) -> Dict[str, Any]:
    """TM-19: injection-path sanity for the emulated touch panel.

    Each --touch tap/down/up must produce its [TOUCH];inj; ack, and a
    display flush must follow within 2 s -- either [REFR] (the harness's
    usual repaint signal) or, with --spitrace left on for the scenario, the
    [SPITRACE] flush sequence advancing. PASS/FAIL is reported per
    assertion: two per tap (ack + flush) plus one pair for a down/up press.
    """
    session.send("--tab 0")
    time.sleep(0.3)
    session.send("--spitrace on")
    time.sleep(0.2)

    flush_pattern = r"\[REFR\]|\[SPITRACE\];flush;"

    def one(cmd: str, ack_pattern: str, label: str) -> Dict[str, Any]:
        idx = session.send(cmd)
        ack = session.wait_for(ack_pattern, 2.0, since=idx)
        flush = session.wait_for(flush_pattern, 2.0, since=idx)
        return {
            "label": label,
            "cmd": cmd,
            "ack_seen": ack is not None,
            "ack_line": ack.string.strip()[:120] if ack else None,
            "flush_seen": flush is not None,
            "passed": ack is not None and flush is not None,
        }

    results = []
    points = TOUCH_TAP_POINTS[: max(1, min(args.touch_tap_count, len(TOUCH_TAP_POINTS)))]
    for x, y in points:
        results.append(one(f"--touch tap {x} {y}", rf"\[TOUCH\];inj;tap;x;{x};y;{y}", f"tap({x},{y})"))
        time.sleep(0.3)

    dx, dy = TOUCH_TAP_POINTS[0]
    results.append(one(f"--touch down {dx} {dy}", rf"\[TOUCH\];inj;down;x;{dx};y;{dy}", f"down({dx},{dy})"))
    time.sleep(0.2)
    results.append(one("--touch up", r"\[TOUCH\];inj;up;", "up"))

    session.send("--spitrace off")
    time.sleep(0.1)

    ok = all(r["passed"] for r in results)
    return {"ok": ok, "results": results}


SCENARIOS: Dict[str, Callable[[TDeckSession, argparse.Namespace], Dict[str, Any]]] = {
    "boot": scenario_boot,
    "idle": scenario_idle,
    "tabs": scenario_tabs,
    "drawer": scenario_drawer,
    "inject": scenario_inject,
    "audio": scenario_audio,
    "audio_stall": scenario_audio_stall,
    "sleep": scenario_sleep,
    "screen": scenario_screen,
    "disptest": scenario_disptest,
    "displaycmd": scenario_displaycmd,
    "uptime": scenario_uptime,
    "map": scenario_map,
    "map_tab_pick": scenario_map_tab_pick,
    "nav": scenario_nav,
    "input": scenario_input,
    "msg_roll": scenario_msg_roll,
    "cdc_backpressure": scenario_cdc_backpressure,
    "heap": scenario_heap,
    "trim": scenario_trim,
    "gps_experiment": scenario_gps_experiment,
    "flush_lora_correlation": scenario_flush_lora_correlation,
    "touch_inject": scenario_touch_inject,
}
SCENARIO_ORDER = [
    "boot",
    "idle",
    "tabs",
    "drawer",
    "inject",
    "audio",
    "audio_stall",
    "sleep",
    "screen",
    "disptest",
    "map",
    "map_tab_pick",
    "nav",
    "input",
    "msg_roll",
    "cdc_backpressure",
    "heap",
    "trim",
    "displaycmd",
    "touch_inject",
]
# gps_experiment, flush_lora_correlation and uptime are long-running
# measurement experiments (multi-minute A/B windows) -- opt in explicitly
# with --scenario, same convention as "uptime" (see SCENARIO_HELP below).


# --------------------------------------------------------------------------
# Reporting
# --------------------------------------------------------------------------


def print_summary(summary: Dict[str, Dict[str, Any]]) -> None:
    print()
    print("=" * 72)
    print("T-Deck Plus bench summary")
    print("=" * 72)
    for name, result in summary.items():
        verdict = "PASS" if result.get("ok") else "FAIL"
        print(f"\n[{name}] {verdict}")
        if name == "boot":
            for msg in result.get("boot_msgs", []):
                print(f"  msg: {msg}")
            print(f"  audio: {result.get('audio_outcome')}")
            print(f"  init: {result.get('init_summary')}")
            if result.get("fail_lines"):
                print(f"  FAIL lines: {len(result['fail_lines'])}")
        elif name == "idle":
            print(
                f"  refr/s={result['refr'].get('refreshes_per_second', 0):.2f}  "
                f"px/s={result['refr'].get('px_per_second', 0):.0f}  "
                f"mean_t_ms={result['refr'].get('mean_t_ms', 0):.1f}  "
                f"max_t_ms={result['refr'].get('max_t_ms', 0)}  "
                f"inv/s={result.get('invalidations_per_second') or 0:.2f}"
            )
            for row in result.get("top10_invalidators_by_class", [])[:5]:
                print(f"    {row['count']:5d}  {row['cls']}/{row['name']}")
            for row in result.get("top10_invalidators_by_backtrace", [])[:5]:
                print(f"    {row['count']:5d}  x{row['objects']} objs  " + " <- ".join(row["frames"]))
        elif name == "tabs":
            for row in result.get("per_tab", []):
                print(
                    f"  tab {row['idx']}: inv={row['inv_count']:4d} refr={row['refr_count']:3d} "
                    f"px={row['px']:6d} repainted={row['repainted_within_500ms']}"
                )
        elif name == "drawer":
            for row in result.get("per_toggle", []):
                print(
                    f"  rep {row['rep']} state={row['state']}: ack={row['drawer_ack']} "
                    f"inv={row['inv_count']:4d} refr={row['refr_count']:3d} px={row['px']:6d}"
                )
        elif name == "inject":
            for row in result.get("results", []):
                print(
                    f"  msg {row['i']}: ok={row['inject_ok']} refr={row['refr_seen']} "
                    f"refr_delta={row.get('refr_delta')} inv={row.get('inv_count')} "
                    f"displayed={row['displayed']} panel={row.get('panel_updated')} msg_list={row['msg_list']}"
                )
                for bt in row.get("top_backtraces", [])[:4]:
                    print(f"      {bt['count']:4d}  " + " <- ".join(bt["frames"]))
        elif name == "audio":
            for row in result.get("results", []):
                print(f"  {row['what']!r}: passed={row['passed']}")
        elif name == "audio_stall":
            ratio = result.get("ratio")
            ratio_str = f"{ratio:.1f}" if ratio is not None else "None"
            print(
                f"  loop_max_us_with_audio={result.get('loop_max_us_with_audio')}  "
                f"loop_max_us_idle={result.get('loop_max_us_idle')}  "
                f"ratio={ratio_str}"
            )
        elif name == "sleep":
            print(
                f"  screen_changed_by_wake={result.get('screen_changed_by_wake')}  "
                f"wake_matches_forced_repaint={result.get('wake_matches_forced_repaint')}  "
                f"full_refresh_after_wake={result.get('full_refresh_after_wake')}  "
                f"max_px_after_wake={result.get('max_px_after_wake')}  "
                f"redraw_count_after_wake={result.get('redraw_count_after_wake')}"
            )
            for bt in result.get("top_backtraces_after_wake", [])[:4]:
                print(f"    {bt['count']:4d}  " + " <- ".join(bt["frames"]))
        elif name == "map":
            if result.get("reason"):
                print(f"  {result['reason']}")
            else:
                st = result.get("stations", [])
                print(
                    f"  home={result.get('home')}  stations_ok={sum(1 for x in st if x['ok'])}/{len(st)}  "
                    f"zoom={result.get('zoom_min')}..{result.get('zoom_max')}  "
                    f"steps={result.get('steps_acked')}/{result.get('steps_total')}  "
                    f"centring_lines={result.get('centring_lines')}  "
                    f"off_centre={result.get('off_centre_steps')}  "
                    f"compose_ms_max={result.get('compose_ms_max')}  crashed={result.get('crashed')}"
                )
                print(
                    f"  inject window: loop_max_us={result.get('inject_loop_max_us')}  "
                    f"map_points={result.get('map_points_after')}"
                )
                for x in st:
                    print(f"    {x['call']} {x['km']:6.1f} km @{x['bearing']:3d}: ok={x['ok']}")
        elif name == "map_tab_pick":
            if result.get("reason"):
                print(f"  {result['reason']}")
            print(
                f"  cursor={result.get('cursor')}  tab_active={result.get('tab_active')}  "
                f"ref_h={result.get('ref_h')}  sdmap_skipped={result.get('sdmap_skipped')}  "
                f"loop_gaps_lvgl={result.get('loop_gaps_lvgl')}"
            )
            for rb in result.get("sdmap_rebuilds", []):
                print(f"    rebuild {rb['w']}x{rb['h']} px, {rb['ms']} ms, from={rb['from']}")
        elif name == "input":
            print(
                f"  keys={result.get('keys_consumed')}/{result.get('keys_sent')} repaints={result.get('key_repaints')} "
                f"latency p50={result.get('key_latency_p50_ms')} p95={result.get('key_latency_p95_ms')} ms"
            )
            for b in result.get("ball", []):
                print(
                    f"    ball {b['dir']:5s}: steps={b['steps']}/{b['requested']} reads={b['events']} "
                    f"moved={b['moved_px']}/{b['moved_expected_px']}px "
                    f"repaints={b['repaints']} latency p50={b['latency_p50_ms']} p95={b['latency_p95_ms']} ms "
                    f"refr p50={b.get('refr_ms_p50')} max={b.get('refr_ms_max')} ms "
                    f"{b['first']}->{b['last']}"
                )
        elif name == "nav":
            print(
                f"  steps={result.get('steps_acked')}/{result.get('steps_total')}  "
                f"not_acked={result.get('not_acked')}  not_repainted={result.get('not_repainted')}  "
                f"settings_scroll_max={result.get('settings_scroll_max')}  "
                f"settings_scroll_final={result.get('settings_scroll_final')}  crashed={result.get('crashed')}"
            )
        elif name == "disptest":
            if result.get("reason"):
                print(f"  {result['reason']}")
            else:
                print(
                    f"  phase={result.get('phase')} stride={result.get('stride')}  "
                    f"steps={result.get('steps_ok')}/{result.get('steps_expected')} ok  "
                    f"seen={result.get('steps_seen')}  device_ms={result.get('device_ms')}  "
                    f"fps={result.get('fps')}"
                )
                print(
                    f"  step ms min/p50/max={result.get('step_ms_min')}/"
                    f"{result.get('step_ms_p50')}/{result.get('step_ms_max')}  "
                    f"wrong_px={result.get('wrong_px')}"
                )
                for m in result.get("mismatches", []):
                    print(f"    MISMATCH {m}")
        elif name == "screen":
            print(
                f"  readback_stable={result.get('readback_stable')}  "
                f"map_has_content={result.get('map_has_content')}"
            )
        elif name == "heap":
            print(f"  delta={result.get('delta')}")
        elif name == "uptime":
            print(f"  steps={result.get('steps')} x {result.get('step_s')}s  input_lat_grew={result.get('input_lat_grew')} loop_max_grew={result.get('loop_max_grew')}")
            for row in result.get("rows", []):
                print(f"  t={row['uptime_s']:5d}s lat_ms={row['input_lat_ms']} loop_max_us={row['loop_max_us']} worst={row['worst_section']}:{row['worst_section_us']} "
                      f"msg_list={row['msg_list']} int_free={row['int_free']} psram_free={row['psram_free']} gaps={len(row['gaps'])} crash={bool(row['crash'])}")
                for g in row["gaps"][:3]:
                    print(f"      {g}")
        elif name == "displaycmd":
            for row in result.get("steps", []):
                print(f"  {row['cmd']:16s} marker={row['marker_seen']} sleeping={row['sleeping']} want={row['want']} ok={row['ok']}")
        elif name == "trim":
            print(
                f"  count={result.get('count')} max_children={result.get('max_children')} "
                f"final={result.get('final_children')} expected={result.get('expected_final')} "
                f"limit={result.get('limit')} inject_fail={result.get('inject_fail')} "
                f"probe_missed={result.get('probe_missed')}"
            )
            print(f"  delta={result.get('delta')}")
        elif name == "gps_experiment":
            print(f"  window_s={result.get('window_seconds')} load={result.get('load_count')}")
            for phase_key, tag in (("phase_gps_on", "GPS ON"), ("phase_gps_off", "GPS OFF")):
                st = result.get(phase_key, {}).get("stats", {})
                print(
                    f"  {tag:8s} p50={st.get('p50_us')} p90={st.get('p90_us')} p99={st.get('p99_us')} "
                    f"max={st.get('max_us')} us  >50ms={st.get('over_50ms_count')} >100ms={st.get('over_100ms_count')}"
                )
            print(f"  verdict: {result.get('verdict')}")
        elif name == "flush_lora_correlation":
            lp, cp = result.get("lora_phase", {}), result.get("control_phase", {})
            print(
                f"  loratx: activity_flushes={lp.get('lora_activity_flushes')} "
                f"with_chg={lp.get('lora_activity_with_chg')}  flush_count={lp.get('flush_count')}"
            )
            print(
                f"  control: activity_flushes={cp.get('lora_activity_flushes')} "
                f"with_chg={cp.get('lora_activity_with_chg')}  flush_count={cp.get('flush_count')}"
            )
            print(f"  conclusion: {result.get('conclusion')}")
        elif name == "touch_inject":
            for row in result.get("results", []):
                print(
                    f"  {row['label']:14s} ack={row['ack_seen']} flush={row['flush_seen']} "
                    f"passed={row['passed']}"
                )
    print()


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------


SCENARIO_HELP = {
    "boot": "inspect the boot log (version, audio outcome, errors)",
    "idle": "redraw/invalidation rate over an idle window",
    "msg_roll": "cursor stall after injected messages + drawer collapse (operator report 2026-09-05)",
    "cdc_backpressure": "prints must not block the loop when the host stops reading the USB CDC port (CDC-01)",
    "tabs": "switch through all tabs, repaint per tab",
    "drawer": "open/close the tab drawer three times",
    "inject": "inject messages via --injectmsg, bubble must be drawn",
    "audio": "start/msg tones and a missing file via --playtone",
    "audio_stall": "loop stall while a tone plays (must stay < 100 ms)",
    "sleep": "display off/on with a message in between",
    "screen": "panel readback probe (void on this hardware) + map content",
    "disptest": "TM-41: colour/geometry sequence, every pushed frame CRC-checked",
    "map": "10 stations at 0.3-400 km, full zoom sweeps, centring, crash watch",
    "map_tab_pick": "TD-14: map tab picked from the tab bar recomposes the SD map twice (bar-visible then bar-hidden height)",
    "nav": "drawer -> tab -> drawer over all tabs, scroll the settings page",
    "input": "keyboard keys and trackball steps through the LVGL indev chain",
    "heap": "heap delta over N injected messages",
    "trim": "TD-03: active-tab message view capped at 50 over N injected messages",
    "displaycmd": "TM-33 (b): --display off/on drives the TFT (sleeping 1/0 via --tft state)",
    "uptime": "TM-30: input latency / loop max / heap per step over a long uptime (not in all)",
    "gps_experiment": "TM-14: A/B loop-time tails with GPS on vs off, fixed load (not in all)",
    "flush_lora_correlation": "TM-06 (c): TFT flush vs LoRa SPI activity correlation (not in all)",
    "touch_inject": "TM-19: injected touch tap/down/up ack + flush-follows sanity",
}

EPILOG = """\
scenario selection:
  --scenario all                 every scenario in the standard order
  --scenario map                 one scenario
  --scenario audio,audio_stall   several, comma separated (run in the given order)
  --scenario all --skip sleep    everything but the named ones
  --list                         print the scenarios and what each one checks

examples:
  python3 tools/bench/tdeck_harness.py --list
  python3 tools/bench/tdeck_harness.py --scenario all --out runs/all.json
  python3 tools/bench/tdeck_harness.py --scenario map --map-stations 10
  python3 tools/bench/tdeck_harness.py --scenario input --input-ball-steps 20
  python3 tools/bench/tdeck_harness.py --scenario inject --inject-count 20 --inject-spacing 2
  python3 tools/bench/tdeck_harness.py --scenario disptest --disptest-phase circle

Opening the serial port reboots the T-Deck; every run starts with a fresh
boot, waits for CLIENT STARTED, then for [BOOT];ready (the network phase
settled), switches the panel on and only then runs the scenarios. The raw
device log of the run lands in tdeck_run_<timestamp>.log in the current
directory (use tools/bench/runs/); the machine-readable verdicts in --out.
"""


def _list_scenarios() -> None:
    print("scenarios (standard order):")
    for name in SCENARIO_ORDER:
        print(f"  {name:12s} {SCENARIO_HELP.get(name, '')}")


def _select_scenarios(spec: str, skip: str) -> List[str]:
    wanted = SCENARIO_ORDER if spec.strip() == "all" else [x.strip() for x in spec.split(",") if x.strip()]
    skipped = {x.strip() for x in skip.split(",") if x.strip()}
    unknown = [x for x in wanted + sorted(skipped) if x not in SCENARIOS]
    if unknown:
        raise SystemExit(f"unknown scenario(s): {', '.join(unknown)} -- see --list")
    return [x for x in wanted if x not in skipped]


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="T-Deck Plus host-side automated regression/measurement harness "
                    "(UI, map, audio, input) over the USB console.",
        epilog=EPILOG,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument(
        "--scenario",
        default="all",
        help="scenario name, comma-separated list, or 'all' (default: all)",
    )
    p.add_argument("--skip", default="", help="comma-separated scenarios to leave out")
    p.add_argument("--list", action="store_true", help="list the scenarios and exit")
    p.add_argument("--port", default=DEFAULT_PORT, help=f"serial port (default: {DEFAULT_PORT})")
    p.add_argument("--heap-count", type=int, default=20, help="injections in the heap scenario (default: 20)")
    p.add_argument("--trim-count", type=int, default=60, help="injections in the trim scenario (default: 60, limit is 50)")
    p.add_argument("--uptime-steps", type=int, default=7, help="probe rounds in the uptime scenario (default: 7)")
    p.add_argument("--uptime-step", type=float, default=300.0, help="seconds between probes in the uptime scenario (default: 300)")
    p.add_argument("--uptime-growth", type=float, default=2.0, help="uptime: fail if last/first exceeds this factor (default: 2.0)")
    p.add_argument("--elf", default=DEFAULT_ELF, help=f"firmware ELF for symbolization (default: {DEFAULT_ELF})")
    p.add_argument(
        "--boot-timeout",
        type=float,
        default=40.0,
        help="seconds to wait for the CLIENT STARTED boot marker (default: 40)",
    )
    p.add_argument(
        "--ready-timeout",
        type=float,
        default=60.0,
        help="seconds to wait for [BOOT];ready after CLIENT STARTED (default: 60)",
    )
    p.add_argument(
        "--idle-seconds", type=float, default=60.0, help="idle scenario window length (default: 60)"
    )
    p.add_argument(
        "--inject-count", type=int, default=5, help="inject scenario message count (default: 5)"
    )
    p.add_argument(
        "--inject-spacing",
        type=float,
        default=3.0,
        help="inject scenario seconds between messages (default: 3)",
    )
    p.add_argument(
        "--disptest-phase",
        default="full",
        choices=["full", "invert", "colors", "square", "circle", "triangle"],
        help="disptest phase to run (default: full = all five)",
    )
    p.add_argument(
        "--disptest-stride",
        type=int,
        default=1,
        help="disptest square/circle growth in pixels per step (default: 1)",
    )
    p.add_argument("--map-stations", type=int, default=10, help="foreign stations injected in the map scenario (default: 10; 40 recycles marker slots)")
    p.add_argument("--input-text", default="bench73", help="text typed in the input scenario (default: bench73)")
    p.add_argument("--input-tab", type=int, default=0, help="tab for the trackball part of the input scenario (default: 0; 3 = map)")
    p.add_argument("--cdc-control-seconds", type=float, default=10.0, help="cdc_backpressure: control roll with the host reading (default: 10)")
    p.add_argument("--cdc-pause-seconds", type=float, default=25.0, help="cdc_backpressure: roll while the host does not read the port (default: 25)")
    p.add_argument("--msgroll-tab", type=int, default=0, help="tab the msg_roll scenario rolls on (default: 0; 3 = map)")
    p.add_argument("--msgroll-cadence-ms", type=int, default=200, help="ms between --ball commands in msg_roll (default: 200; 60 overruns the 1-byte-per-loop serial reader)")
    p.add_argument("--msgroll-seconds", type=float, default=12.0, help="trackball roll duration per phase in the msg_roll scenario (default: 12)")
    p.add_argument("--msgroll-msgs", type=int, default=3, help="messages injected between the two rolls in msg_roll (default: 3)")
    p.add_argument("--msgroll-gap-ms", type=int, default=400, help="[BALL] read gap that counts as a stall in msg_roll (default: 400; a SD map recomposition costs 470-750 ms, a clean roll stays under 250)")
    p.add_argument("--input-ball-steps", type=int, default=10, help="trackball steps per direction in the input scenario (default: 10)")
    p.add_argument("--maptab-settle-seconds", type=float, default=4.0, help="map_tab_pick: seconds collected after the click before checking rebuilds (default: 4.0)")
    p.add_argument(
        "--gps-window", type=float, default=120.0, help="gps_experiment: per-phase capture window seconds (default: 120)"
    )
    p.add_argument(
        "--flush-window", type=float, default=60.0, help="flush_lora_correlation: per-phase capture window seconds (default: 60)"
    )
    p.add_argument(
        "--flush-force-pause",
        type=float,
        default=0.3,
        help="flush_lora_correlation: seconds between forced-redraw drawer toggles (default: 0.3)",
    )
    p.add_argument("--loratx-count", type=int, default=10, help="flush_lora_correlation: --loratx frame count (default: 10)")
    p.add_argument(
        "--loratx-interval-ms", type=int, default=500, help="flush_lora_correlation: --loratx inter-frame ms (default: 500)"
    )
    p.add_argument(
        "--touch-tap-count",
        type=int,
        default=3,
        help="touch_inject: number of tap positions to use, 1-3 (default: 3)",
    )
    p.add_argument("--out", default="summary.json", help="output JSON summary path (default: summary.json)")
    args = p.parse_args(argv)
    if args.list:
        _list_scenarios()
        raise SystemExit(0)
    args.scenarios = _select_scenarios(args.scenario, args.skip)
    return args


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)

    session = TDeckSession(port=args.port, boot_timeout=args.boot_timeout, ready_timeout=args.ready_timeout)
    try:
        session.open()
    except TimeoutError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2
    except Exception as e:
        print(f"ERROR: could not open session on {args.port}: {e}", file=sys.stderr)
        return 2

    order = args.scenarios
    summary: Dict[str, Dict[str, Any]] = {}
    overall_ok = True
    try:
        for name in order:
            print(f"=== running scenario: {name} ===", file=sys.stderr)
            result = SCENARIOS[name](session, args)
            summary[name] = result
            overall_ok = overall_ok and bool(result.get("ok", True))
    finally:
        session.close()

    print_summary(summary)
    with open(args.out, "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2, default=str)
    print(f"summary written to {args.out}")

    return 0 if overall_ok else 1


if __name__ == "__main__":
    sys.exit(main())
