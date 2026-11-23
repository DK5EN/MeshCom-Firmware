#!/usr/bin/env python3
"""OTA-LOWHEAP-01 knockout runs R0-R7: heap cost per feature, measured with Auto Update check + install.

Per run: toggle one feature against the node's own baseline (a feature that is on is switched off, one that is
off is switched on, so the cost is always "on minus off"), reboot, settle, take idle heap samples, run
`--update check` + `--update install` (stage only, never apply), parse the AU heap lines, `--update forget`,
then restore the baseline and reboot again. The baseline comes from `--info` at the start; every run ends with
the node back on it, and the tool re-reads `--info` at the end and reports any difference.

Runs (operator OK 2026-11-23 for DK5EN-1 and the T-Beam, R4 included):
  R0 baseline   R1 web server   R2 KISS   R3 AppLink   R4 gateway + EXTUDP   R5 mailbox (store)
  R6 display (control)   R7 everything that is on, off together

Outputs under --outdir:  knockout-<call>.log (every console line), knockout.summary (one line per run).
Usage:  ota_lowheap_knockout.py --target DK5EN-1=<ip> [--runs R0,R1] [--settle 90] [--samples 3]
"""

from __future__ import annotations

import argparse
import re
import sys
import time
from datetime import datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ota_lowheap_trigger as trig  # noqa: E402  (Session, heap_sample, wait_done, verdict)

DEFAULT_OUT = Path(__file__).resolve().parent / "runs/harness/ota-lowheap-knockout-20261123"

# feature -> (regex on --info giving on/off, command stem). Store uses "own" as its on-mode.
FEATURES = {
    "web": (r"Webserver\s+(on|off)", "--webserver"),
    "kiss": (r"KISS/TCP\s+(on|off)", "--kiss"),
    "applink": (r"AppLink\s+(on|off)", "--applink"),
    "gateway": (r"Gateway\s+(on|off)", "--gateway"),
    "extudp": (r"EXTUDP\s+(on|off)", "--extudp"),
    "store": (r"STORE mode=(\w+)", "--store"),
    "display": (r"\.\.\.DISPLAY\s+(on|off)", "--display"),
}
RUNS = {
    "R0": [],
    "R1": ["web"],
    "R2": ["kiss"],
    "R3": ["applink"],
    "R4": ("off", ["gateway", "extudp"]),  # off-only: never switch EXTUDP on (no peer configured)
    "R5": ["store"],
    "R6": ["display"],
    "R7": "all-on-off",
}
AU_RE = {
    "gate": re.compile(r"\[AU\];heap;gate;free;(\d+);iblk;(\d+)"),
    "peak": re.compile(r"\[AU\];heap;peak;free;(\d+);iblk;(\d+);min;(\d+);lowest;(\d+)"),
    "fails": re.compile(r"\[AU\];alloc;end;fails;(\d+)"),
}


# Everything this tool may send: the trigger tool's read/AU set plus the feature toggles and a reboot. Never
# "--update apply", never a gateway server / WiFi / password setter.
ALLOWED = set(trig.ALLOWED) | {
    "--update unstage",
    "--reboot",
    "--store own",
    "--store off",
    "--store list",
    "--store heard",
}
ALLOWED |= {
    f"{stem} {v}" for _, stem in FEATURES.values() if stem != "--store" for v in ("on", "off")
}


class KoSession(trig.Session):
    def cmd(self, c: str, wait: float = 4.0) -> list[str]:
        if c not in ALLOWED:
            raise RuntimeError(f"command not allowed: {c}")
        self.w(f"[KO] >>> {c}")
        self.con.send(c)
        return self.drain(wait)


def parse_state(info: list[str]) -> dict[str, str]:
    txt = "\n".join(info)
    st = {}
    for f, (rx, _) in FEATURES.items():
        m = re.search(rx, txt)
        st[f] = m.group(1) if m else "?"
    return st


def is_on(f: str, v: str) -> bool:
    return v not in ("off", "?") if f == "store" else v == "on"


def set_cmd(f: str, on: bool, baseline: str) -> str:
    stem = FEATURES[f][1]
    if f == "store":
        return f"{stem} {baseline if on and is_on(f, baseline) else ('own' if on else 'off')}"
    return f"{stem} {'on' if on else 'off'}"


def plan(run: str, base: dict[str, str]) -> list[tuple[str, bool]]:
    """(feature, target_on) pairs for this run; empty for R0."""
    spec = RUNS[run]
    if spec == "all-on-off":
        return [(f, False) for f, v in base.items() if is_on(f, v)]
    if isinstance(spec, tuple):  # ("off", features): only switch off what is on
        return [(f, False) for f in spec[1] if is_on(f, base[f])]
    return [(f, not is_on(f, base[f])) for f in spec if base[f] != "?"]


def reboot_and_reconnect(s: KoSession, settle: float) -> None:
    try:
        s.cmd("--reboot", 2.0)  # the node closes the console when it resets: expected
    except ConnectionError:
        pass
    s.con.close()  # only the socket: Session.close() would also close the log file the reconnect writes to
    time.sleep(25)
    s.connect(tries=20)
    s.drain(settle)


def au_round(s: KoSession) -> dict:
    got: list[str] = []
    t0 = time.monotonic()
    got += s.cmd("--update check", 3.0)
    got += trig.wait_done(s, 180, t0)
    t1 = time.monotonic()
    shed = 0
    try:
        got += s.cmd("--update install", 3.0)
        got += trig.wait_done(s, 600, t1)
    except ConnectionError:
        # Fix A sheds the HMAC console during the AU job on a tight node (T-Beam): not a reset. The download's
        # heap lines are only on the serial port then; reconnect once the job restored the console.
        shed = 1
        s.con.close()
        s.w("[KO] console shed by the AU job, reconnecting after the job")
        time.sleep(45)
        s.connect(tries=40)
        got += trig.wait_done(s, 600, time.monotonic())
    st = s.cmd("--update status", 2.0)
    got += st
    txt = "\n".join(got)
    gates = AU_RE["gate"].findall(txt)
    peaks = AU_RE["peak"].findall(txt)
    fails = [int(x) for x in AU_RE["fails"].findall(txt)]
    r = {"verdict": trig.verdict(got), "fails": sum(fails), "console_shed": shed}
    m = re.findall(r"\[AU\];status;.*;err;([^;]+);", "\n".join(st))
    if m:
        r["status_err"] = m[-1]
    if gates:
        r["check_gate_free"] = int(gates[0][0])
        r["dl_gate_free"] = int(gates[-1][0])
    if peaks:
        r["check_lowest"] = int(peaks[0][3])
        r["dl_lowest"] = int(peaks[-1][3])
        if "check_gate_free" in r:
            r["check_used"] = r["check_gate_free"] - r["check_lowest"]
        if "dl_gate_free" in r and len(peaks) > 1:
            r["dl_used"] = r["dl_gate_free"] - r["dl_lowest"]
    s.cmd("--update forget", 2.0)
    # drop the staged image, else the next install is "already_staged"
    s.cmd("--update unstage", 2.0)
    return r


def run_node(call: str, host: str, a: argparse.Namespace) -> int:
    outdir: Path = a.outdir
    outdir.mkdir(parents=True, exist_ok=True)
    s = KoSession(call, host, outdir / f"knockout-{call}.log")
    drift = 0
    try:
        s.connect()
        base = parse_state(s.cmd("--info", 5.0))
        s.cmd("--update unstage", 2.0)  # a staged image from earlier work would skip the download
        s.w(f"[KO] baseline {base}")
        for run in a.runs:
            steps = plan(run, base)
            for f, on in steps:
                s.cmd(set_cmd(f, on, base[f]), 3.0)
            reboot_and_reconnect(s, a.settle)
            samples = [trig.heap_sample(s) for _ in range(a.samples)]
            frees = [int(x["free"]) for x in samples if str(x["free"]).isdigit()]
            idle = sorted(frees)[len(frees) // 2] if frees else -1
            au = au_round(s)
            line = (
                f"{datetime.now():%Y-%m-%d %H:%M:%S} {call} {run} toggled="
                f"{','.join(f'{f}:{int(on)}' for f, on in steps) or '-'} idle_free={idle} "
                + " ".join(f"{k}={v}" for k, v in au.items())
            )
            with open(outdir / "knockout.summary", "a") as fh:
                fh.write(line + "\n")
            s.w(f"[KO] {line}")
            if steps:
                for f, on in steps:
                    s.cmd(set_cmd(f, not on, base[f]), 3.0)
                reboot_and_reconnect(s, 20)
        end = parse_state(s.cmd("--info", 5.0))
        for f in base:
            if end[f] != base[f]:
                drift += 1
                s.w(f"[KO] RESTORE MISMATCH {f}: baseline {base[f]} now {end[f]}")
        with open(outdir / "knockout.summary", "a") as fh:
            fh.write(
                f"{datetime.now():%Y-%m-%d %H:%M:%S} {call} restore={'ok' if not drift else f'MISMATCH {drift}'}\n"
            )
    finally:
        s.close()
    return 1 if drift else 0


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("--target", action="append", required=True, metavar="CALL=HOST")
    ap.add_argument("--outdir", type=Path, default=DEFAULT_OUT)
    ap.add_argument("--runs", default="R0,R1,R2,R3,R4,R5,R6,R7")
    ap.add_argument(
        "--settle", type=float, default=90.0, help="seconds after reboot before sampling"
    )
    ap.add_argument("--samples", type=int, default=3)
    a = ap.parse_args()
    a.runs = [r.strip() for r in a.runs.split(",") if r.strip()]
    bad = [r for r in a.runs if r not in RUNS]
    if bad:
        ap.error(f"unknown runs {bad}")
    rc = 0
    for t in a.target:
        call, host = t.split("=", 1)
        rc |= run_node(call, host, a)
    return rc


if __name__ == "__main__":
    sys.exit(main())
