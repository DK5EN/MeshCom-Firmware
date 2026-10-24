#!/usr/bin/env python3
"""TRK-02 measurement: do web-flasher DTR/RTS sequences press the PRG button?

Campaign docs/campaign-trk02-webflash-track-20261024.md. On a Heltec V3 the
CP2102 auto-reset circuit pulls GPIO0 (PRG / user button) LOW with DTR
asserted and RTS deasserted, and EN LOW with RTS asserted and DTR
deasserted. The MeshCom OneButton handlers treat 3+ clicks as "toggle TRACK
and save it". This tool replays the esptool-js control-line sequences (and a
few port open/close patterns) against a node and counts what the firmware
makes of them. It is a measurement tool, not a fix.

Per run: make sure the app answers, read TRACK (`--pos` prints
`...Track: on|off`; `--info` does not), replay one strategy with timestamps,
capture every serial line (through a reset if the sequence caused one), read
TRACK again, and switch TRACK off again if the run changed it.

Commands sent to the node: `--info`, `--pos`, `--setcont on` (only with
--setup), `--track off` (only to undo a TRACK change). Nothing transmits.

Exit codes: 0 = measured, expectation held; 1 = finding (button event or TRACK
change where none was expected); 2 = harness error (port, identity guard,
app not answering, positive control without events).

    python3 tools/bench/dtr_replay.py --node DK5EN-1 --strategy all --runs 10
    python3 tools/bench/dtr_replay.py --node DK5EN-1 --strategy click_train \\
        --runs 3 --expect-events --usb-serial 0001
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
from collections.abc import Callable, Sequence
from datetime import datetime
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))
from identity_guard import IdentityError, load_fleet, require_node

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_RUNS_DIR = REPO_ROOT / "tools" / "bench" / "runs" / "harness"
BAUD = 115200
READ_TIMEOUT = 0.1
APP_READY_TIMEOUT = 20.0
CALL_RE = re.compile(r"\.\.\.Call: <")
READY_RE = re.compile(r"\[BOOT\];ready")
TRACK_RE = re.compile(r"\.\.\.Track: (on|off)\b", re.IGNORECASE)
TRACK_JSON_RE = re.compile(r'"TRACK"\s*:\s*(true|false)', re.IGNORECASE)

# Events counted per run. The first five are button events; the rest are
# reset / ROM markers.
BUTTON_EVENTS = (
    "SingleClick",
    "DoubleClick",
    "TripleClick",
    "LongPress",
    "BUTTON single",
    "Track on",
)
EVENT_PATTERNS: dict[str, re.Pattern[str]] = {
    "SingleClick": re.compile(r"SingleClick"),
    "DoubleClick": re.compile(r"DoubleClick"),
    "TripleClick": re.compile(r"TripleClick"),
    "LongPress": re.compile(r"LongPress"),
    "BUTTON single": re.compile(r"BUTTON single"),
    "Track on": re.compile(r"Track on - "),
    "RESET_REASON": re.compile(r"RESET_REASON"),
    "waiting for download": re.compile(r"waiting for download"),
    "boot:": re.compile(r"boot:"),
}
RESET_KEYS = ("RESET_REASON", "boot:")
ROM_KEYS = ("waiting for download",)

# --- strategy tables -------------------------------------------------------
# Step grammar (one tuple per step):
#   ("dtr", 0|1)      port.dtr = value; remembered as "last DTR"
#   ("rts", 0|1)      port.rts = value
#   ("setrts", 0|1)   esptool-js setRTS(x): rts = x, then dtr = last DTR (two transfers)
#   ("sleep", ms)     capture serial lines for ms
#   ("close",)        close the port
#   ("reopen", "default"|"safe")  default = pyserial defaults (driver decides
#                     DTR/RTS), safe = dtr=False, rts=False set before open()

Step = tuple[Any, ...]


# esptool-js Wr (classic reset), resetDelay 50 or 550.
def _classic(delay_ms: int) -> list[Step]:
    return [
        ("dtr", 0),
        ("setrts", 1),
        ("sleep", 100),
        ("dtr", 1),
        ("setrts", 0),
        ("sleep", delay_ms),
        ("dtr", 0),
    ]


# esptool-js Vr (hard reset, UART): setRTS(1); sleep 100; setRTS(0).
HARDRESET: list[Step] = [("setrts", 1), ("sleep", 100), ("setrts", 0)]

# Plain reset used to leave ROM download mode: RTS 1 for 120 ms with DTR 0.
RECOVER: list[Step] = [("dtr", 0), ("setrts", 1), ("sleep", 120), ("setrts", 0)]

STRATEGIES: dict[str, list[Step]] = {
    # esptool-js Wr, resetDelay 50 / 550 (W0 report).
    "classic50": _classic(50),
    "classic550": _classic(550),
    # esptool-js Zr (USB-JTAG-Serial reset).
    "usbjtag": [
        ("setrts", 0),
        ("dtr", 0),
        ("sleep", 100),
        ("dtr", 1),
        ("setrts", 0),
        ("sleep", 100),
        ("setrts", 1),
        ("dtr", 0),
        ("setrts", 1),
        ("sleep", 100),
        ("setrts", 0),
        ("dtr", 0),
    ],
    # esptool-js Vr (hard reset, UART).
    "hardreset": list(HARDRESET),
    # Post-install reset of the flasher (W0 report): RTS 100 ms, then hardreset tail.
    "wa": [("setrts", 1), ("sleep", 100), ("sleep", 100), ("setrts", 0)],
    # Console reset button of the flasher: same wire pattern as wa (W0 report).
    "console_reset": [("setrts", 1), ("sleep", 100), ("sleep", 100), ("setrts", 0)],
    # Two-transfer setRTS alone with DTR last = 1: a deliberate IO0-low hold
    # while the app runs (documents what one short press does).
    "rts_then_dtr": [("dtr", 1), ("setrts", 0), ("sleep", 200), ("dtr", 0)],
    # Port closed and reopened with the OS/driver default DTR/RTS, 3x.
    "open_close": [
        ("close",),
        ("reopen", "default"),
        ("sleep", 1000),
        ("close",),
        ("reopen", "safe"),
    ]
    * 3,
    # Positive control: three DTR pulses (120 ms high, 200 ms low), RTS 0.
    "click_train": [("rts", 0)]
    + [s for _ in range(3) for s in (("dtr", 1), ("sleep", 120), ("dtr", 0), ("sleep", 200))],
}
# Strategies that end in ROM download mode or reset the chip and need the recover reset.
NEEDS_RECOVER = {"classic50", "classic550", "usbjtag"}
ALL_STRATEGIES = [n for n in STRATEGIES if n != "click_train"]


class HarnessError(Exception):
    """Bench setup broken -- not a finding."""


# --- parsing / counting ----------------------------------------------------


def parse_track(text: str) -> bool | None:
    """TRACK state from `--pos` output (`...Track: on|off`, src/command_functions.cpp:7429)
    or, failing that, a settings JSON (`"TRACK":true`, :7635). Last match wins."""
    last: bool | None = None
    pos = -1
    for rx, conv in (
        (TRACK_RE, lambda s: s.lower() == "on"),
        (TRACK_JSON_RE, lambda s: s.lower() == "true"),
    ):
        for m in rx.finditer(text):
            if m.start() >= pos:
                pos, last = m.start(), conv(m.group(1))
    return last


def count_events(lines: Sequence[str]) -> dict[str, int]:
    counts = dict.fromkeys(EVENT_PATTERNS, 0)
    for line in lines:
        for name, rx in EVENT_PATTERNS.items():
            if rx.search(line):
                counts[name] += 1
    return counts


def button_events(counts: dict[str, int]) -> int:
    return sum(counts[k] for k in BUTTON_EVENTS)


def run_verdict(
    counts: dict[str, int], track_before: bool | None, track_after: bool | None, expect_events: bool
) -> str:
    """ok / finding / control_ok / control_failed."""
    changed = track_before is not None and track_after is not None and track_before != track_after
    seen = button_events(counts) > 0
    if expect_events:
        return "control_ok" if counts["TripleClick"] > 0 else "control_failed"
    return "finding" if (seen or changed) else "ok"


def summarize(strategy: str, records: Sequence[dict[str, Any]]) -> dict[str, Any]:
    verdicts = [r["verdict"] for r in records]
    if "control_failed" in verdicts:
        verdict = "control_failed"
    elif "finding" in verdicts:
        verdict = "finding"
    elif verdicts and all(v == "control_ok" for v in verdicts):
        verdict = "control_ok"
    else:
        verdict = "ok"
    return {
        "strategy": strategy,
        "runs": len(records),
        "runs_with_events": sum(1 for r in records if button_events(r["events"]) > 0),
        "runs_track_changed": sum(1 for r in records if r["track_changed"]),
        "verdict": verdict,
    }


def exit_code(summaries: Sequence[dict[str, Any]]) -> int:
    verdicts = {s["verdict"] for s in summaries}
    if "control_failed" in verdicts:
        return 2
    if "finding" in verdicts:
        return 1
    return 0


# --- serial session --------------------------------------------------------

Opener = Callable[[str], Any]  # mode "safe" | "default" -> open serial object


def make_opener(port: str) -> Opener:
    import serial  # pyserial

    def opener(mode: str) -> Any:
        if mode == "default":
            # pyserial defaults on purpose: the OS/driver decides DTR/RTS at open.
            return serial.Serial(port, BAUD, timeout=READ_TIMEOUT)
        s = serial.Serial()
        s.port, s.baudrate, s.timeout = port, BAUD, READ_TIMEOUT
        s.dtr, s.rts = False, False  # before open(), or the bridge pulses the lines
        s.open()
        return s

    return opener


class Session:
    """Serial port plus timestamped line capture. clock/sleep are injectable."""

    def __init__(self, opener: Opener, clock: Callable[[], float] = time.monotonic) -> None:
        self.opener = opener
        self.clock = clock
        self.ser: Any = opener("safe")
        self.t0 = clock()
        self.last_dtr = 0
        self.lines: list[tuple[int, str]] = []
        self.steps: list[dict[str, Any]] = []
        self._buf = b""

    def t_ms(self) -> int:
        return int(round((self.clock() - self.t0) * 1000))

    def reset_clock(self) -> None:
        self.t0 = self.clock()
        self.lines = []
        self.steps = []

    def close(self) -> None:
        if self.ser is not None:
            self.ser.close()
            self.ser = None

    def _feed(self, data: bytes) -> None:
        if not data:
            return
        self._buf += data
        while b"\n" in self._buf:
            raw, self._buf = self._buf.split(b"\n", 1)
            self.lines.append((self.t_ms(), raw.decode(errors="replace").rstrip("\r")))

    def pump(self, seconds: float) -> None:
        end = self.clock() + seconds
        while self.clock() < end:
            if self.ser is None:
                raise HarnessError("port closed while capturing")
            self._feed(self.ser.read(4096))

    def send(self, cmd: str) -> None:
        self.ser.write(cmd.encode() + b"\n")
        self.ser.flush()

    def wait_for(self, pattern: re.Pattern[str], timeout: float, since: int) -> bool:
        end = self.clock() + timeout
        while True:
            if any(pattern.search(t) for _, t in self.lines[since:]):
                return True
            if self.clock() >= end:
                return False
            self._feed(self.ser.read(4096))

    def text_since(self, idx: int) -> str:
        return "\n".join(t for _, t in self.lines[idx:])

    def do_step(self, step: Step) -> None:
        op = step[0]
        rec: dict[str, Any] = {"op": op, "t_ms": self.t_ms()}
        if op == "dtr":
            self.ser.dtr = bool(step[1])
            self.last_dtr = int(step[1])
            rec["value"] = int(step[1])
        elif op == "rts":
            self.ser.rts = bool(step[1])
            rec["value"] = int(step[1])
        elif op == "setrts":
            self.ser.rts = bool(step[1])
            self.ser.dtr = bool(self.last_dtr)
            rec["value"] = int(step[1])
            rec["dtr"] = self.last_dtr
        elif op == "sleep":
            rec["ms"] = int(step[1])
            self.steps.append(rec)
            self.pump(step[1] / 1000.0)
            return
        elif op == "close":
            self.close()
        elif op == "reopen":
            self.ser = self.opener(step[1])
            self.last_dtr = 0
            rec["mode"] = step[1]
        else:
            raise HarnessError(f"unknown step {step!r}")
        self.steps.append(rec)

    def run_steps(self, table: Sequence[Step]) -> None:
        for step in table:
            self.do_step(step)


def ensure_app_up(s: Session) -> None:
    """--info until a `...Call: <` answer; waits for [BOOT];ready after a reboot."""
    for _ in range(3):
        idx = len(s.lines)
        s.send("")
        s.send("--info")
        if s.wait_for(CALL_RE, 6.0, idx):
            return
        s.wait_for(READY_RE, APP_READY_TIMEOUT, idx)
    raise HarnessError("app does not answer --info")


def read_track(s: Session) -> bool | None:
    idx = len(s.lines)
    s.send("--pos")
    s.wait_for(TRACK_RE, 6.0, idx)
    s.pump(0.5)
    return parse_track(s.text_since(idx))


def run_once(
    s: Session, strategy: str, run: int, settle: float, expect_events: bool
) -> dict[str, Any]:
    ensure_app_up(s)
    track_before = read_track(s)
    s.reset_clock()
    start = len(s.lines)
    s.run_steps(STRATEGIES[strategy])
    if strategy in NEEDS_RECOVER:
        s.run_steps(RECOVER)
    s.pump(settle)
    counts = count_events([t for _, t in s.lines[start:]])
    reset_seen = any(counts[k] for k in RESET_KEYS)
    rom_seen = any(counts[k] for k in ROM_KEYS)
    if reset_seen or rom_seen:
        s.wait_for(READY_RE, APP_READY_TIMEOUT, start)
        s.pump(2.0)
        counts = count_events([t for _, t in s.lines[start:]])
    captured = list(s.lines)
    track_after = read_track(s)
    changed = track_before is not None and track_after is not None and track_before != track_after
    restored: bool | None = None
    if changed:
        s.send("--track off")
        s.pump(1.5)
        restored = read_track(s) is False
    return {
        "strategy": strategy,
        "run": run,
        "steps": list(s.steps),
        "lines": [{"t_ms": t, "text": x} for t, x in captured],
        "events": counts,
        "track_before": track_before,
        "track_after": track_after,
        "track_changed": changed,
        "track_restored": restored,
        "reset_seen": reset_seen,
        "rom_seen": rom_seen,
        "verdict": run_verdict(counts, track_before, track_after, expect_events),
    }


# --- CLI -------------------------------------------------------------------


def resolve_port(args: argparse.Namespace) -> str:
    if args.port:
        return args.port
    serial_no = args.usb_serial
    if not serial_no:
        serial_no = load_fleet()["nodes"].get(args.node, {}).get("usb_serial")
    if not serial_no:
        raise HarnessError(
            f"no --port/--usb-serial and fleet.json has no usb_serial for {args.node}"
        )
    from serial.tools import list_ports  # pyserial

    for p in list_ports.comports():
        if p.serial_number == serial_no:
            return str(p.device)
    raise HarnessError(f"no USB serial port with serial number {serial_no!r}")


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(
        description="TRK-02: replay DTR/RTS sequences, count button events / TRACK changes"
    )
    ap.add_argument("--node", required=True, help="fleet.json node name (identity guard)")
    ap.add_argument("--port", default=None, help="serial device")
    ap.add_argument("--usb-serial", default=None, help="USB serial number (resolved to a device)")
    ap.add_argument("--strategy", default="all", choices=[*STRATEGIES, "all"])
    ap.add_argument("--runs", type=int, default=10)
    ap.add_argument(
        "--expect-events", action="store_true", help="positive control: events are expected"
    )
    ap.add_argument("--setup", action="store_true", help="send --setcont on once before the matrix")
    ap.add_argument(
        "--out",
        default=None,
        help="JSONL path (default tools/bench/runs/harness/trk02-replay-<node>-<ts>.jsonl)",
    )
    ap.add_argument(
        "--settle", type=float, default=3.0, help="seconds of capture after each sequence"
    )
    return ap


def main(
    argv: Sequence[str] | None = None,
    *,
    opener_factory: Callable[[str], Opener] = make_opener,
    guard: Callable[[str, str], Any] = require_node,
    resolver: Callable[[argparse.Namespace], str] = resolve_port,
    clock: Callable[[], float] = time.monotonic,
) -> int:
    args = build_parser().parse_args(argv)
    names = ALL_STRATEGIES if args.strategy == "all" else [args.strategy]
    try:
        port = resolver(args)
        guard(args.node, port)
        stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
        out = (
            Path(args.out)
            if args.out
            else DEFAULT_RUNS_DIR / f"trk02-replay-{args.node}-{stamp}.jsonl"
        )
        out.parent.mkdir(parents=True, exist_ok=True)
        summary_path = out.with_suffix(".summary")
        s = Session(opener_factory(port), clock)
        summaries: list[dict[str, Any]] = []
        try:
            ensure_app_up(s)
            if args.setup:
                s.send("--setcont on")
                s.pump(1.0)
            with out.open("w", encoding="utf-8") as fh:
                for name in names:
                    records = []
                    for run in range(1, args.runs + 1):
                        rec = run_once(s, name, run, args.settle, args.expect_events)
                        fh.write(json.dumps(rec) + "\n")
                        fh.flush()
                        records.append(rec)
                        print(
                            f"{name} run {run}: {rec['verdict']} events={button_events(rec['events'])} track {rec['track_before']}->{rec['track_after']}"
                        )
                    summaries.append(summarize(name, records))
        finally:
            s.close()
        text = "".join(
            f"{x['strategy']} runs={x['runs']} runs_with_events={x['runs_with_events']} "
            f"runs_track_changed={x['runs_track_changed']} verdict={x['verdict']}\n"
            for x in summaries
        )
        summary_path.write_text(text, encoding="utf-8")
        print(text, end="")
        return exit_code(summaries)
    except (HarnessError, IdentityError, OSError) as exc:
        print(f"harness error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
