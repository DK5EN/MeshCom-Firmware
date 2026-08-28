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
import glob
import json
import re
import subprocess
import sys
import threading
import time
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional, Sequence, Tuple

try:
    import serial  # pyserial
except ImportError:  # pragma: no cover - exercised only when pyserial is missing
    serial = None  # type: ignore[assignment]

from tdeck_parse import heap_delta, parse_line, redraw_summary, refr_summary

DEFAULT_PORT = "/dev/cu.usbmodem1101"
DEFAULT_BAUD = 115200
DEFAULT_ELF = ".pio/build/t_deck_plus/firmware.elf"
BOOT_MARKER = "CLIENT STARTED"


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
    ) -> None:
        self.port = port
        self.baud = baud
        self.boot_timeout = boot_timeout
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
        home / ".platformio/packages/toolchain-xtensa-esp-elf*/bin/xtensa-esp32s3-elf-addr2line",
        home / ".platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-addr2line",
        home / ".platformio/packages/toolchain-xtensa-esp32s3*/bin/xtensa-esp32s3-elf-addr2line",
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
            [tool, "-pfiaC", "-e", elf, *uniq],
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
    addrs = [ra for ra, _ in top_ra if ra]
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
    for i in range(args.inject_count):
        t_start = time.monotonic()
        idx = session.send(f"--injectmsg 9999 bench inject {i}")
        inject_ok = session.wait_for(r"\[INJECT\];ok", 2.0, since=idx) is not None
        refr_seen = session.wait_for(r"\[REFR\]", 1.5, since=idx) is not None
        audio_lines = [l for _, _, l in session.records_since(idx) if l.startswith("[AUDIO]")]
        post = get_uistat(session)
        msg_list = post.get("msg_list") if post else None
        growth = (
            msg_list - prev_msg_list
            if (msg_list is not None and prev_msg_list is not None)
            else None
        )
        displayed = inject_ok and refr_seen
        results.append(
            {
                "i": i,
                "inject_ok": inject_ok,
                "refr_seen": refr_seen,
                "displayed": displayed,
                "msg_list": msg_list,
                "msg_list_growth": growth,
                "audio_lines": audio_lines,
            }
        )
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
        m = session.wait_for(pattern, 3.0, since=idx)
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


def scenario_heap(session: TDeckSession, args: argparse.Namespace) -> Dict[str, Any]:
    idx0 = session.send("--heap h0")
    m0 = session.wait_for(r"\[INSTR-HEAP\];h0;", 2.0, since=idx0)
    h0 = parse_line(m0.string) if m0 else None

    for i in range(20):
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


SCENARIOS: Dict[str, Callable[[TDeckSession, argparse.Namespace], Dict[str, Any]]] = {
    "idle": scenario_idle,
    "tabs": scenario_tabs,
    "drawer": scenario_drawer,
    "inject": scenario_inject,
    "audio": scenario_audio,
    "heap": scenario_heap,
}
SCENARIO_ORDER = ["idle", "tabs", "drawer", "inject", "audio", "heap"]


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
        if name == "idle":
            print(
                f"  refr/s={result['refr'].get('refreshes_per_second', 0):.2f}  "
                f"px/s={result['refr'].get('px_per_second', 0):.0f}  "
                f"mean_t_ms={result['refr'].get('mean_t_ms', 0):.1f}  "
                f"max_t_ms={result['refr'].get('max_t_ms', 0)}  "
                f"inv/s={result.get('invalidations_per_second') or 0:.2f}"
            )
            for row in result.get("top10_invalidators_by_class", [])[:5]:
                print(f"    {row['count']:5d}  {row['cls']}/{row['name']}")
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
                    f"displayed={row['displayed']} msg_list={row['msg_list']}"
                )
        elif name == "audio":
            for row in result.get("results", []):
                print(f"  {row['what']!r}: passed={row['passed']}")
        elif name == "heap":
            print(f"  delta={result.get('delta')}")
    print()


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="T-Deck Plus host-side automated regression/measurement harness."
    )
    p.add_argument(
        "--scenario",
        choices=["all", *SCENARIO_ORDER],
        default="all",
        help="which scenario(s) to run (default: all)",
    )
    p.add_argument("--port", default=DEFAULT_PORT, help=f"serial port (default: {DEFAULT_PORT})")
    p.add_argument("--elf", default=DEFAULT_ELF, help=f"firmware ELF for symbolization (default: {DEFAULT_ELF})")
    p.add_argument(
        "--boot-timeout",
        type=float,
        default=40.0,
        help="seconds to wait for the CLIENT STARTED boot marker (default: 40)",
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
    p.add_argument("--out", default="summary.json", help="output JSON summary path (default: summary.json)")
    return p.parse_args(argv)


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)

    session = TDeckSession(port=args.port, boot_timeout=args.boot_timeout)
    try:
        session.open()
    except TimeoutError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2
    except Exception as e:
        print(f"ERROR: could not open session on {args.port}: {e}", file=sys.stderr)
        return 2

    order = SCENARIO_ORDER if args.scenario == "all" else [args.scenario]
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
