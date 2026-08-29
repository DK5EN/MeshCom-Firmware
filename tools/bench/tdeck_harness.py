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
        self.log_path = log_path or Path(
            f"tdeck_run_{time.strftime('%Y%m%d-%H%M%S')}.log"
        )
        self.lines: List[Tuple[float, float, str]] = []
        self._lock = threading.Lock()
        self._log_lock = threading.Lock()
        self._stop = threading.Event()
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
            idx = self.send("--uistat")
            if self.wait_for(r"\[UISTAT\]", 2.0, since=idx) is not None:
                answered = True
                break
        if not answered:
            raise TimeoutError("device never answered --uistat within 60s after boot")
        # Answering --uistat is not the end of initialisation: the WiFi attempt
        # (up to ~15 s, plus one radio reset and retry) still runs, the header
        # icons blink and commands are serviced late. The firmware prints
        # [BOOT];ready;ms;<millis> once bAllStarted is true; wait for it, but
        # tolerate its absence (older firmware) after ready_timeout.
        m = self.wait_for(re.escape(READY_MARKER), self.ready_timeout, since=0)
        self.boot_ready_ms = None
        if m:
            mm = re.search(r"ready;ms;(\d+)", m.string)
            self.boot_ready_ms = int(mm.group(1)) if mm else None
        if m is None:
            self._write_raw_log(
                f"## no {READY_MARKER} within {self.ready_timeout}s -- continuing anyway"
            )
        # The panel goes dark TDECK_TFT_TIMEOUT (30 s) after the last key or
        # touch, serial traffic does not count; switch it on for the eye tests.
        self.send("--tft on")
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

    def _reader_loop(self) -> None:
        assert self.ser is not None
        while not self._stop.is_set():
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
        idx = session.send(f"--injectmsg 9999 bench inject {i}")
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
    for i, (km, bearing) in enumerate(MAP_STATIONS[: args.map_stations], start=1):
        slat, slon = _offset(lat, lon, km, bearing)
        call = f"DK5EN-{i:02d}"
        idx = session.send(f"--injectpos {call} {slat:.5f} {slon:.5f}")
        m = session.wait_for(r"\[INJECTPOS\];(ok|err)|" + CRASH_PATTERN, 6.0, since=idx)
        ok = m is not None and "INJECTPOS];ok" in m.string
        stations.append({"call": call, "km": km, "bearing": bearing, "lat": round(slat, 5),
                         "lon": round(slon, 5), "ok": ok,
                         "line": m.string.strip()[:100] if m else None})
        if not ok:
            inject_ok = False
        time.sleep(0.6)

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
        idx = session.send(f"--ball {direction} {n}")
        session.wait_for(r"\[BALL\];inject;", 2.0, since=idx)
        time.sleep(0.3 + 0.02 * n)
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


def scenario_heap(session: TDeckSession, args: argparse.Namespace) -> Dict[str, Any]:
    idx0 = session.send("--heap h0")
    m0 = session.wait_for(r"\[INSTR-HEAP\];h0;", 2.0, since=idx0)
    h0 = parse_line(m0.string) if m0 else None

    for i in range(args.heap_count):
        t_start = time.monotonic()
        session.send(f"--injectmsg 9999 heap probe {i}")
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
    inject_idx = session.send("--injectmsg 9999 sleep probe")
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
    "map": scenario_map,
    "nav": scenario_nav,
    "input": scenario_input,
    "heap": scenario_heap,
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
    "map",
    "nav",
    "input",
    "heap",
]


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
                for x in st:
                    print(f"    {x['call']} {x['km']:6.1f} km @{x['bearing']:3d}: ok={x['ok']}")
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
        elif name == "screen":
            print(
                f"  readback_stable={result.get('readback_stable')}  "
                f"map_has_content={result.get('map_has_content')}"
            )
        elif name == "heap":
            print(f"  delta={result.get('delta')}")
    print()


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------


SCENARIO_HELP = {
    "boot": "inspect the boot log (version, audio outcome, errors)",
    "idle": "redraw/invalidation rate over an idle window",
    "tabs": "switch through all tabs, repaint per tab",
    "drawer": "open/close the tab drawer three times",
    "inject": "inject messages via --injectmsg, bubble must be drawn",
    "audio": "start/msg tones and a missing file via --playtone",
    "audio_stall": "loop stall while a tone plays (must stay < 100 ms)",
    "sleep": "display off/on with a message in between",
    "screen": "panel readback probe (void on this hardware) + map content",
    "map": "10 stations at 0.3-400 km, full zoom sweeps, centring, crash watch",
    "nav": "drawer -> tab -> drawer over all tabs, scroll the settings page",
    "input": "keyboard keys and trackball steps through the LVGL indev chain",
    "heap": "heap delta over N injected messages",
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
    p.add_argument("--map-stations", type=int, default=10, help="foreign stations injected in the map scenario (default: 10)")
    p.add_argument("--input-text", default="bench73", help="text typed in the input scenario (default: bench73)")
    p.add_argument("--input-tab", type=int, default=0, help="tab for the trackball part of the input scenario (default: 0; 3 = map)")
    p.add_argument("--input-ball-steps", type=int, default=10, help="trackball steps per direction in the input scenario (default: 10)")
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
