#!/usr/bin/env python3
"""Heltec V3 long-press deep-sleep regression test (DS-03).

Guards the fix in fork commit 40c29e7f (PR #1140): the long-press handler
reaches --deepsleep 800 ms into a held button press; the shared sleep
helpers used to arm the same pin as an ext1 wake source before the press
was released, so the wake condition was already true at sleep entry and the
node rebooted at once instead of sleeping. See
docs/deepsleep-button-autotest.md for the full design.

On the Heltec V3 the CP2102 USB bridge wires DTR to GPIO0 (the PRG button),
so `ser.dtr = True` is an electrically identical button press and
`ser.dtr = False` releases it -- no external actuator needed. RTS must
never be touched: RTS toggles EN (a plain reset) and would prove nothing
about deep sleep.

    python3 tools/bench/deepsleep_button.py
    python3 tools/bench/deepsleep_button.py --port /dev/cu.usbserial-0001 --cycles 3
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
from collections.abc import Sequence
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))
from oled_harness import OledSession
from tdeck_harness import TDeckSession
from identity_guard import IdentityError, require

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_PORT = "/dev/cu.usbserial-0001"  # Heltec V3 DK5EN-93
DEFAULT_RUNS_DIR = "tools/bench/runs"
READY_TIMEOUT = 60.0
BOOT_SIGNATURE = re.compile(
    r"RESET_REASON=|CLIENT SETUP|rst:0x|ets Jul|Guru Meditation|Backtrace"
)


class HarnessError(Exception):
    """Something about the bench setup is broken -- not a firmware regression."""


class DeepsleepSession(OledSession):
    """OledSession with a caller-chosen log_path instead of OledSession's own
    hardcoded, differently-timestamped default -- so the raw log lines up
    with this script's --json/--runs-dir naming. Calls TDeckSession.__init__
    directly (skipping OledSession.__init__, which pins its own log_path)
    and then applies the same probe/wake overrides OledSession would.
    """

    def __init__(
        self, port: str, boot_timeout: float, ready_timeout: float, log_path: Path
    ) -> None:
        TDeckSession.__init__(
            self,
            port=port,
            boot_timeout=boot_timeout,
            ready_timeout=ready_timeout,
            log_path=log_path,
        )
        # --oledstat is an INSTRUMENT_ENABLED bench command and absent from the
        # shipping image; --info exists everywhere and answers with the call.
        self.probe_cmd = "--info"
        self.probe_pattern = r"\.\.\.Call:"
        self.wake_cmd = None


def run_cycle(
    s: DeepsleepSession, args: argparse.Namespace, cycle_num: int
) -> dict[str, Any]:
    """One long-press / release / wake cycle. Raises HarnessError for
    bench-level failures (no long press seen); records a firmware regression
    (early reboot, wrong wake reset) in the returned dict instead."""
    rec: dict[str, Any] = {"cycle": cycle_num}

    btn_idx = s.send("--button on")
    obut_m = s.wait_for(
        r"\[OBUT\]\.\.\.One Button GPIO\(\d+\) started", 5.0, since=btn_idx
    )
    rec["obut_ack"] = obut_m is not None
    idx = btn_idx

    t_press = time.monotonic()
    s.ser.dtr = True
    go_m = s.wait_for(r"GO to deepsleep", args.hold + 1.0, since=idx)
    t_go = time.monotonic()
    if go_m is None:
        s.ser.dtr = False
        raise HarnessError("no long press seen -- DTR may not reach GPIO0")
    rec["go_after_ms"] = round((t_go - t_press) * 1000)
    remaining = args.hold - (t_go - t_press)
    if remaining > 0:
        time.sleep(remaining)
    s.ser.dtr = False
    t_release = time.monotonic()

    time.sleep(args.quiet)
    regression_line: str | None = None
    regression_delay_ms: int | None = None
    quiet_lines: list[str] = []
    for _, t_mono, line in s.records_since(idx):
        if BOOT_SIGNATURE.search(line):
            if regression_line is None:
                regression_line = line
                regression_delay_ms = round((t_mono - t_release) * 1000)
        elif t_mono >= t_release:
            quiet_lines.append(line)
    rec["quiet_ok"] = regression_line is None
    rec["regression_line"] = regression_line
    rec["regression_delay_ms"] = regression_delay_ms
    rec["quiet_lines"] = quiet_lines

    if regression_line is None:
        rec.update(do_wake(s, args))
    else:
        # The node already rebooted on its own: it is awake, a wake pulse would
        # be a plain button click on a running node. Record the regression and
        # leave the wake fields empty.
        rec.update(
            {
                "wake_reset_num": None,
                "wake_reset": None,
                "wake_cause_num": None,
                "wake_cause": None,
                "ready_seen": None,
                "boot_ms": None,
                "alive": None,
            }
        )
    rec["ok"] = bool(
        rec["quiet_ok"] and rec["wake_reset"] == "DEEPSLEEP" and rec["alive"]
    )
    return rec


def do_wake(s: DeepsleepSession, args: argparse.Namespace) -> dict[str, Any]:
    """Pulse DTR to wake the node, then confirm it came back the deep-sleep
    way (RESET_REASON=... DEEPSLEEP) and is alive again."""
    wake_idx = s.length()
    s.ser.dtr = True
    time.sleep(args.wake_pulse)
    s.ser.dtr = False

    reset_m = s.wait_for(r"\[BOOT\] RESET_REASON=(\d+) (\w+)", 10.0, since=wake_idx)
    if reset_m is None:
        raise HarnessError("no [BOOT] RESET_REASON line within 10s of the wake pulse")
    wake_reset_num = int(reset_m.group(1))
    wake_reset_name = reset_m.group(2)

    # WAKE_CAUSE is a sibling-agent addition (not yet in every image) --
    # capture it when present, never fail on its absence.
    cause_m = s.wait_for(r"\[BOOT\] WAKE_CAUSE=(\d+) (\w+)", 2.0, since=wake_idx)
    wake_cause_num = int(cause_m.group(1)) if cause_m else None
    wake_cause_name = cause_m.group(2) if cause_m else None

    started_m = s.wait_for(
        re.escape("CLIENT STARTED"), args.boot_timeout, since=wake_idx
    )
    if started_m is None:
        raise HarnessError(
            f"CLIENT STARTED not seen within {args.boot_timeout}s after the wake pulse"
        )

    ready_m = s.wait_for(r"\[BOOT\][; ]ready", 60.0, since=wake_idx)
    boot_ms: int | None = None
    if ready_m is not None:
        mm = re.search(r"ready[; ]ms[; ](\d+)", ready_m.string)
        boot_ms = int(mm.group(1)) if mm else None
    else:
        time.sleep(5.0)  # tolerate absence (older firmware), just give it a moment

    info_idx = s.send("--info")
    alive = s.wait_for(r"\.\.\.Call:", 5.0, since=info_idx) is not None

    return {
        "wake_reset_num": wake_reset_num,
        "wake_reset": wake_reset_name,
        "wake_cause_num": wake_cause_num,
        "wake_cause": wake_cause_name,
        "ready_seen": ready_m is not None,
        "boot_ms": boot_ms,
        "alive": alive,
    }


def print_table(cycles: list[dict[str, Any]]) -> None:
    print(
        f"{'cyc':>3}  {'go_ms':>6}  {'quiet_ok':>8}  {'wake_reset':>12}  "
        f"{'wake_cause':>12}  {'boot_ms':>7}  {'alive':>5}"
    )
    for c in cycles:
        print(
            f"{c.get('cycle'):>3}  {c.get('go_after_ms')!s:>6}  "
            f"{c.get('quiet_ok')!s:>8}  {c.get('wake_reset')!s:>12}  "
            f"{c.get('wake_cause')!s:>12}  {c.get('boot_ms')!s:>7}  "
            f"{c.get('alive')!s:>5}"
        )


def main(argv: Sequence[str] | None = None) -> int:
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    p.add_argument("--port", default=DEFAULT_PORT)
    p.add_argument("--cycles", type=int, default=2)
    p.add_argument(
        "--hold", type=float, default=2.0, help="DTR hold for the long press, s"
    )
    p.add_argument(
        "--quiet", type=float, default=15.0, help="no-reboot window after release, s"
    )
    p.add_argument("--wake-pulse", type=float, default=0.2, help="DTR pulse to wake, s")
    p.add_argument("--boot-timeout", type=float, default=40.0)
    p.add_argument(
        "--keep-button",
        action="store_true",
        help="skip restoring the original button setting",
    )
    p.add_argument(
        "--runs-dir",
        default=DEFAULT_RUNS_DIR,
        help="relative to the repo root unless absolute",
    )
    p.add_argument(
        "--json", default=None, help="default: <runs-dir>/deepsleep_button_<ts>.json"
    )
    p.add_argument(
        "--node", default=None,
        help="fleet.json node name for the identity guard (required unless --no-identity-guard)",
    )
    p.add_argument(
        "--no-identity-guard", action="store_true",
        help="skip the identity guard -- only for setting up a node's identity",
    )
    args = p.parse_args(argv)
    if not args.node and not args.no_identity_guard:
        p.error(
            "--node NAME is required (fleet.json node name) unless --no-identity-guard "
            "is given for identity setup"
        )

    runs_dir = Path(args.runs_dir)
    if not runs_dir.is_absolute():
        runs_dir = REPO_ROOT / runs_dir
    runs_dir.mkdir(parents=True, exist_ok=True)

    ts = time.strftime("%Y%m%d-%H%M%S")
    log_path = runs_dir / f"deepsleep_button_{ts}.log"
    json_path = (
        Path(args.json) if args.json else runs_dir / f"deepsleep_button_{ts}.json"
    )

    s = DeepsleepSession(args.port, args.boot_timeout, READY_TIMEOUT, log_path)
    cycles: list[dict[str, Any]] = []
    error: str | None = None
    button_before: str | None = None
    try:
        try:
            s.open()
        except Exception as e:
            raise HarnessError(f"could not open session: {e}") from e

        if args.node and not args.no_identity_guard:
            guard_idx = s.send("--info")
            s.wait_for(r"\.\.\.Call:", 8.0, since=guard_idx)
            info_text = "\n".join(l for _, _, l in s.records_since(guard_idx))
            try:
                require(info_text, args.node)
            except IdentityError as e:
                raise HarnessError(str(e)) from e

        info_idx = s.send("--info")
        m = s.wait_for(r"BUTTON \(0\) (on|off)", 5.0, since=info_idx)
        button_before = m.group(1) if m else None

        for i in range(1, args.cycles + 1):
            rec = run_cycle(s, args, i)
            cycles.append(rec)
            if not rec["quiet_ok"]:
                break  # regression proven; further cycles only add button presses
    except HarnessError as e:
        error = str(e)
    finally:
        if not args.keep_button and button_before is not None and s.ser is not None:
            try:
                s.send(f"--button {button_before}")
                time.sleep(0.5)
            except OSError as e:
                print(
                    f"warning: could not restore --button {button_before}: {e}",
                    file=sys.stderr,
                )
        s.close()

    if error is not None:
        verdict, exit_code = "ERROR", 2
    elif all(c.get("ok") for c in cycles):
        verdict, exit_code = "PASS", 0
    else:
        verdict, exit_code = "REGRESSION", 1

    print_table(cycles)
    print(f"verdict: {verdict}" + (f" ({error})" if error else ""))

    summary = {
        "port": args.port,
        "args": vars(args),
        "verdict": verdict,
        "error": error,
        "button_before": button_before,
        "log_path": str(log_path),
        "cycles": cycles,
    }
    with open(json_path, "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2, default=str)
    print(f"summary written to {json_path}")
    return exit_code


if __name__ == "__main__":
    sys.exit(main())
