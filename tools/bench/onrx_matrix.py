#!/usr/bin/env python3
"""OnRxDone flags-matrix driver and analyser for the Heltec V3 bench node DK5EN-1.

Driver: identity guard, HMAC login on the 2323 net console, flag arm, N minutes of
capture, restore of every setting the run changed. It sends ONLY settings commands
(no message, beacon, injectraw or broadcast).

Analyser: parses the instrumentation markers (key=value tokens, unknown tokens
ignored)

    [INS] ONRX seq=<n> total_us=<u> print_us=<u> bytes=<u> len=<b> irq_to_read_us=<i> ph=<n>:<us>,... [lost=<n>] [txbuf=<bytes>/<ok>]
    [INS] ONRX_READY                                  (boot marker)

and falls back to the production line ``[MC-DBG] ONRXDONE_TIME ms=<n>`` (no
print_us) when a capture holds no ONRX lines.

Usage:
    onrx_matrix.py --host <ip> --arm A|B|C|D|F --minutes N [--out DIR]
    onrx_matrix.py --analyse <logfile> [--arm LABEL]       (offline, no network)

Command spellings (src/command_functions.cpp):
    --loradebug on|off  :2906 / :2924 (on also sets bDisplayInfo + bDisplayRetx, persists sset 0x0200)
    --setlog on|off     :442-443 (toggle table; persisted in node_sset4; "--setlog <call>" also exists)
    --nbrdebug on|off   :469-470 (toggle table, persisted)
    --setinfo on|off    :438-439 (bDisplayInfo, RAM only)
    --setretx on|off    :444-445 (bDisplayRetx, RAM only)
    state readback      :6751 (...LORADEBUG/NBRDEBUG) and :6768 (...DisplayInfo/...DisplyLog) of --info
    bDisplayRetx is not printed by --info: restored to the pre-run LORADEBUG state
    (loradebug on/off sets info and retx together), an approximation.

Field semantics:
    irq_to_read_us   -1 means "no IRQ stamp" (injected frame, missed-edge recovery). Such records
                     are dropped from the irq statistics and counted in irq_no_stamp; 0 is a valid
                     latency and is kept.
    txbuf=<B>/<ok>   UART TX buffer size and whether it was applied; arm F must show ok=1, B>0.
                     Absent in old logs (reported as None).
    ph=name:us,...   each value is the time since the PREVIOUS stamp, not since function entry.
                     A stamp for a branch that was not taken is absent from the record; its time
                     lands in the next stamp that is present. Per-phase p50/p99/max are taken over
                     the records that carry the phase.

Exit codes of the driver: 1 identity/pre-state refusal, 2 restore errors (settings may be left
changed on the node), 3 no instrument markers, 4 capture error (restore clean), 5 arm F without
an applied txbuf.
"""

from __future__ import annotations

import argparse
import hashlib
import hmac
import json
import math
import os
import re
import socket
import sys
import time
from datetime import datetime
from pathlib import Path
from typing import Any, Callable, Iterable, Protocol

REPO = Path(__file__).resolve().parents[2]
DEFAULT_OUT = REPO / "tools" / "bench" / "runs" / "harness"
NODE = "heltec-1"   # fleet.json key (callsign is CALL)
CALL = "DK5EN-1"
OVER_US = 50_000

# arm -> commands, in order. F is flag-identical to A (differs by firmware image only).
_A = ["--loradebug on", "--setlog on", "--nbrdebug on"]
ARM_COMMANDS: dict[str, list[str]] = {
    "A": list(_A),
    "B": ["--loradebug off", "--setlog on", "--nbrdebug on"],
    "C": ["--loradebug off", "--setlog on", "--nbrdebug off"],
    "D": ["--loradebug off", "--setlog off", "--nbrdebug off"],
    "F": list(_A),
}

# ---------------------------------------------------------------- parsing

_ONRX = re.compile(r"\[INS\] ONRX\s+(\S.*)$")
_READY = re.compile(r"\[INS\] ONRX_READY")
_UARTBUF = re.compile(r"\[INS\] UARTTXBUF\s+(\S.*)$")
_TIME = re.compile(r"ONRXDONE_TIME\s+ms=(\d+)")
_TXBUF = re.compile(r"^(\d+)/(\d+)$")
_INT_KEYS = ("seq", "total_us", "print_us", "bytes", "len", "irq_to_read_us", "lost")


def _tokens(rest: str) -> dict[str, str]:
    out: dict[str, str] = {}
    for tok in rest.split():
        if "=" in tok:
            k, _, v = tok.partition("=")
            out[k] = v
    return out


def parse_ph(text: str) -> dict[str, int]:
    out: dict[str, int] = {}
    for part in text.split(","):
        name, sep, val = part.partition(":")
        if sep and name:
            try:
                out[name] = int(val)
            except ValueError:
                pass
    return out


def parse_log(lines: Iterable[str]) -> dict[str, Any]:
    """Return {'onrx': [dict], 'time_ms': [int], 'ready': bool, 'malformed': int, 'uartbuf': str|None}."""
    onrx: list[dict[str, Any]] = []
    time_ms: list[int] = []
    ready = False
    malformed = 0
    uartbuf: str | None = None
    for line in lines:
        if _READY.search(line):
            ready = True
            continue
        mu = _UARTBUF.search(line)
        if mu:
            uartbuf = mu.group(1).strip()
            continue
        m = _ONRX.search(line)
        if m:
            tok = _tokens(m.group(1))
            rec: dict[str, Any] = {}
            try:
                rec["total_us"] = int(tok["total_us"])
                for k in _INT_KEYS:
                    if k != "total_us" and k in tok:
                        rec[k] = int(tok[k])
            except (KeyError, ValueError):
                malformed += 1
                continue
            if "ph" in tok:
                rec["ph"] = parse_ph(tok["ph"])
            mb = _TXBUF.match(tok.get("txbuf", ""))
            if mb:
                rec["txbuf"] = tok["txbuf"]
                rec["txbuf_bytes"] = int(mb.group(1))
                rec["txbuf_ok"] = int(mb.group(2))
            onrx.append(rec)
            continue
        mt = _TIME.search(line)
        if mt:
            time_ms.append(int(mt.group(1)))
    return {"onrx": onrx, "time_ms": time_ms, "ready": ready, "malformed": malformed, "uartbuf": uartbuf}


# ------------------------------------------------------------- statistics


def pct(values: list[float], p: float) -> float | None:
    """Percentile with linear interpolation (numpy default). None for empty input."""
    if not values:
        return None
    s = sorted(values)
    if len(s) == 1:
        return float(s[0])
    pos = (len(s) - 1) * p / 100.0
    lo = math.floor(pos)
    hi = min(lo + 1, len(s) - 1)
    return float(s[lo] + (s[hi] - s[lo]) * (pos - lo))


def dist(values: list[float]) -> dict[str, float | None]:
    return {
        "p50": pct(values, 50),
        "p90": pct(values, 90),
        "p99": pct(values, 99),
        "max": float(max(values)) if values else None,
    }


def linfit(xs: list[float], ys: list[float]) -> dict[str, float | None]:
    """Ordinary least squares y = slope*x + intercept; R2 = 1 - ss_res/ss_tot."""
    n = len(xs)
    if n < 2:
        return {"slope": None, "intercept": None, "r2": None}
    mx = sum(xs) / n
    my = sum(ys) / n
    sxx = sum((x - mx) ** 2 for x in xs)
    if sxx == 0:
        return {"slope": None, "intercept": None, "r2": None}
    sxy = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
    slope = sxy / sxx
    icpt = my - slope * mx
    ss_tot = sum((y - my) ** 2 for y in ys)
    ss_res = sum((y - (slope * x + icpt)) ** 2 for x, y in zip(xs, ys))
    r2 = 1.0 if ss_tot == 0 else 1.0 - ss_res / ss_tot
    return {"slope": slope, "intercept": icpt, "r2": r2}


def analyse(lines: Iterable[str], arm: str = "?") -> dict[str, Any]:
    parsed = parse_log(lines)
    recs = parsed["onrx"]
    src = "ONRX"
    if not recs and parsed["time_ms"]:
        recs = [{"total_us": ms * 1000} for ms in parsed["time_ms"]]
        src = "ONRXDONE_TIME"
    elif not recs:
        src = "none"
    totals = [float(r["total_us"]) for r in recs]
    have_print = [r for r in recs if "print_us" in r]
    res: dict[str, Any] = {
        "arm": arm,
        "source": src,
        "n": len(recs),
        "malformed": parsed["malformed"],
        "ready_marker": parsed["ready"],
        "uarttxbuf": parsed["uartbuf"],
        "total_us": dist(totals),
        "over_50ms_total": sum(1 for t in totals if t > OVER_US),
    }
    if src == "ONRXDONE_TIME":
        res["note"] = "no print_us (fallback to ONRXDONE_TIME, 1 ms resolution)"
    res["onrxdone_time_cross"] = {
        "n": len(parsed["time_ms"]),
        "p50_ms": pct([float(x) for x in parsed["time_ms"]], 50),
    }
    if have_print:
        pr = [float(r["print_us"]) for r in have_print]
        tt = [float(r["total_us"]) for r in have_print]
        rest = [t - p for t, p in zip(tt, pr)]
        res["print_us"] = dist(pr)
        res["total_minus_print_us"] = dist(rest)
        res["print_share"] = (sum(pr) / sum(tt)) if sum(tt) > 0 else None
        res["over_50ms_total_minus_print"] = sum(1 for v in rest if v > OVER_US)
        withb = [r for r in have_print if "bytes" in r]
        res["fit_total_vs_bytes"] = linfit(
            [float(r["bytes"]) for r in withb], [float(r["total_us"]) for r in withb]
        )
        res["fit_n"] = len(withb)
    irq_all = [r["irq_to_read_us"] for r in recs if "irq_to_read_us" in r]
    irq = [float(v) for v in irq_all if v >= 0]
    res["irq_n"] = len(irq)
    res["irq_no_stamp"] = len(irq_all) - len(irq)  # -1 = no IRQ stamp; 0 is a valid latency
    res["irq_to_read_us"] = {k: v for k, v in dist(irq).items() if k in ("p50", "p99", "max")}
    seen = [r["txbuf"] for r in recs if "txbuf" in r]
    res["txbuf"] = sorted(set(seen)) if seen else None
    res["txbuf_applied"] = any(r.get("txbuf_ok") == 1 and r.get("txbuf_bytes", 0) > 0 for r in recs)
    phases: dict[str, list[float]] = {}
    for r in recs:
        for name, us in r.get("ph", {}).items():
            phases.setdefault(name, []).append(float(us))
    res["phases"] = {
        name: {"n": len(v), "p50": pct(v, 50), "p99": pct(v, 99), "max": float(max(v))}
        for name, v in phases.items()
    }
    lost = [r["lost"] for r in recs if "lost" in r]
    res["lost_max"] = max(lost) if lost else None
    return res


def _f(v: float | None, spec: str = ".0f") -> str:
    return "-" if v is None else format(v, spec)


def format_table(r: dict[str, Any]) -> str:
    out = [f"arm {r['arm']}  source {r['source']}  n {r['n']}  malformed {r['malformed']}  "
           f"ready_marker {r['ready_marker']}"]
    if r.get("note"):
        out.append(f"  note: {r['note']}")
    out.append("  metric                      p50        p90        p99        max   (us)")

    def row(name: str, d: dict[str, Any] | None) -> None:
        if d is None:
            out.append(f"  {name:<24} {'no print_us':>10}")
        else:
            out.append(f"  {name:<24} {_f(d.get('p50')):>10} {_f(d.get('p90')):>10} "
                       f"{_f(d.get('p99')):>10} {_f(d.get('max')):>10}")

    row("total_us", r["total_us"])
    row("print_us", r.get("print_us"))
    row("total-print_us", r.get("total_minus_print_us"))
    out.append(f"  over 50 ms: total {r['over_50ms_total']}  total-print "
               f"{r.get('over_50ms_total_minus_print', '-')}  print share {_f(r.get('print_share'), '.3f')}")
    fit = r.get("fit_total_vs_bytes")
    if fit:
        out.append(f"  fit total_us = {_f(fit['slope'], '.2f')} us/B * bytes + {_f(fit['intercept'], '.0f')}  "
                   f"R2 {_f(fit['r2'], '.3f')}  (n {r.get('fit_n')})")
    irq = r["irq_to_read_us"]
    out.append(f"  irq_to_read_us p50 {_f(irq.get('p50'))} p99 {_f(irq.get('p99'))} max {_f(irq.get('max'))}  "
               f"(n {r['irq_n']}, no stamp {r['irq_no_stamp']})  "
               f"lost max {r['lost_max'] if r['lost_max'] is not None else '-'}")
    out.append(f"  txbuf {','.join(r['txbuf']) if r['txbuf'] else '-'}")
    for name, d in r["phases"].items():
        out.append(f"  ph {name:<16} n {d['n']:>6}  p50 {_f(d['p50']):>8} p99 {_f(d['p99']):>8} "
                   f"max {_f(d['max']):>8}  (us since previous stamp)")
    return "\n".join(out)


# ---------------------------------------------------------------- driver


class ConsoleLike(Protocol):
    def send(self, cmd: str) -> None: ...
    def drain(self, seconds: float, sink: Callable[[str], None]) -> None: ...


class NetConsole:
    """2323 net console with the HMAC login (same protocol as identity_guard.read_info_net)."""

    def __init__(self, host: str, password: str, port: int = 2323) -> None:
        self.sock = socket.create_connection((host, port), timeout=10)
        self.sock.settimeout(0.3)
        self._pending = b""
        out = b""
        end = time.time() + 1.5
        while time.time() < end:
            try:
                out += self.sock.recv(4096)
            except socket.timeout:
                pass
            if out.startswith(b"NONCE:") and b"\n" in out:
                break
        if out.startswith(b"NONCE:"):
            if not password:
                raise RuntimeError("console wants a password (set MC_CONSOLE_PW)")
            nonce = bytes.fromhex(out.split(b"\n", 1)[0].split()[1].decode())
            self.sock.sendall(hmac.new(password.encode(), nonce, hashlib.sha256).hexdigest().encode() + b"\n")
            self.sock.settimeout(3)
            reply = self.sock.recv(64)
            if not reply.startswith(b"OK"):
                raise RuntimeError("console login refused")
            self.sock.settimeout(0.3)
        else:
            self._pending = out

    def send(self, cmd: str) -> None:
        self.sock.sendall(cmd.encode() + b"\r\n")

    def drain(self, seconds: float, sink: Callable[[str], None]) -> None:
        end = time.time() + seconds
        while time.time() < end:
            try:
                d = self.sock.recv(4096)
            except socket.timeout:
                continue
            if not d:
                raise RuntimeError("console closed the connection")
            self._pending += d
            while b"\n" in self._pending:
                line, self._pending = self._pending.split(b"\n", 1)
                sink(line.decode("utf-8", "replace").rstrip("\r"))

    def close(self) -> None:
        self.sock.close()


_STATE_RE = {
    "loradebug": re.compile(r"\.\.\.LORADEBUG (on|off)"),
    "nbrdebug": re.compile(r"\.\.\.NBRDEBUG (on|off)"),
    "displayinfo": re.compile(r"\.\.\.DisplayInfo (on|off)"),
    "displaylog": re.compile(r"\.\.\.DisplyLog (on|off)"),
}


def parse_state(info_text: str) -> dict[str, bool | None]:
    """Pre-run values of the settings this script changes, from a --info reply."""
    st: dict[str, bool | None] = {}
    for k, rx in _STATE_RE.items():
        ms = list(rx.finditer(info_text))
        st[k] = (ms[-1].group(1) == "on") if ms else None
    st["retx"] = st["loradebug"]  # approximation, see module docstring
    return st


def _oo(flag: bool) -> str:
    return "on" if flag else "off"


def restore_commands(st: dict[str, bool | None]) -> list[str]:
    """loradebug first (it rewrites info+retx), then the individual settings."""
    cmds: list[str] = []
    for key, fmt in (
        ("loradebug", "--loradebug {}"),
        ("displayinfo", "--setinfo {}"),
        ("retx", "--setretx {}"),
        ("displaylog", "--setlog {}"),
        ("nbrdebug", "--nbrdebug {}"),
    ):
        v = st.get(key)
        if v is not None:
            cmds.append(fmt.format(_oo(v)))
    return cmds


def _restore(console: ConsoleLike, pre: dict[str, bool | None], sink: Callable[[str], None]) -> list[str]:
    errors: list[str] = []
    for cmd in restore_commands(pre):
        try:
            console.send(cmd)
            console.drain(0.5, sink)
        except Exception as exc:  # keep restoring the rest
            errors.append(f"{cmd}: {exc}")
    return errors


def run_arm(
    console: ConsoleLike,
    arm: str,
    pre: dict[str, bool | None],
    capture_s: float,
    sink: Callable[[str], None],
    reconnect: Callable[[], ConsoleLike] | None = None,
) -> tuple[str | None, list[str]]:
    """Apply the arm, capture, always restore. Returns (capture_error, restore_errors).

    If the restore fails on the original connection (typically because the capture died with the
    connection), ``reconnect`` opens ONE fresh console and the whole restore is retried there.
    A KeyboardInterrupt restores too, then propagates.
    """
    capture_error: str | None = None
    fatal: BaseException | None = None
    try:
        for cmd in ARM_COMMANDS[arm]:
            console.send(cmd)
            console.drain(1.0, sink)
        console.drain(capture_s, sink)
    except BaseException as exc:  # noqa: BLE001 - restore must run on every exit path
        capture_error = f"{type(exc).__name__}: {exc}"
        if not isinstance(exc, Exception):
            fatal = exc
    errors = _restore(console, pre, sink)
    if errors and reconnect is not None:
        first = errors
        fresh: ConsoleLike | None = None
        try:
            fresh = reconnect()
            errors = _restore(fresh, pre, sink)
        except Exception as exc:
            errors = first + [f"reconnect: {exc}"]
        finally:
            closer = getattr(fresh, "close", None)
            if callable(closer):
                try:
                    closer()
                except Exception:
                    pass
    if fatal is not None:
        raise fatal
    return capture_error, errors


def write_report(log: Path, arm: str) -> dict[str, Any]:
    res = analyse(log.read_text(errors="replace").splitlines(), arm)
    log.with_suffix(".json").write_text(json.dumps(res, indent=2) + "\n")
    print(format_table(res))
    return res


def drive(host: str, arm: str, minutes: float, out: Path) -> int:
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import identity_guard as ig

    pw = os.environ.get("MC_CONSOLE_PW", "")
    fleet = ig.load_fleet()
    problems = ig.fleet_problems(fleet)
    if problems:
        print("fleet.json inconsistent: " + "; ".join(problems))
        return 1
    try:
        text = ig.read_info_net(host, password=pw)
        info = ig.require(text, NODE, fleet)
    except ig.IdentityError as exc:
        print(exc)
        return 1
    if info.call != CALL or info.txpower_dbm is None or info.txpower_dbm > 2:
        print(f"refused: need {CALL} at <= 2 dBm, got {info.call} {info.txpower_dbm} dBm")
        return 1
    pre = parse_state(text)
    print(f"{NODE} ok ({info.call}, {info.txpower_dbm} dBm); pre-run state {pre}")
    return run_session(lambda: NetConsole(host, pw), arm, pre, minutes, out)


def missing_pre_state(pre: dict[str, bool | None]) -> list[str]:
    """Settings the arms change whose pre-run value is unknown (cannot be restored)."""
    return [k for k in ("loradebug", "nbrdebug", "displaylog") if pre.get(k) is None]


def run_session(
    connect: Callable[[], ConsoleLike],
    arm: str,
    pre: dict[str, bool | None],
    minutes: float,
    out: Path,
) -> int:
    """Pre-state check, arm + capture + restore, report. Returns the process exit code."""
    missing = missing_pre_state(pre)
    if missing:
        print("refused: pre-run state of " + ", ".join(missing) + " unknown (truncated --info reply?); "
              "the arm would change it and it could not be restored. No arm command was sent.")
        return 1
    out.mkdir(parents=True, exist_ok=True)
    log = out / f"onrx_{arm}_{datetime.now().strftime('%Y%m%d-%H%M%S')}.log"
    try:
        console = connect()
    except Exception as exc:
        print(f"cannot open the console: {exc} (no arm command was sent)")
        return 4
    capture_error: str | None = None
    errors: list[str] = []
    with log.open("w") as fh:
        def sink(line: str) -> None:
            fh.write(line + "\n")
            fh.flush()

        try:
            capture_error, errors = run_arm(console, arm, pre, minutes * 60.0, sink, reconnect=connect)
        finally:
            closer = getattr(console, "close", None)
            if callable(closer):
                try:
                    closer()
                except Exception:
                    pass
    rc = 0
    if errors:
        print("RESTORE ERRORS: " + "; ".join(errors))
        print(f"WARNING: the node may keep persisted loradebug/setlog/nbrdebug values; pre-run state was {pre}")
        rc = 2
    if capture_error:
        print(f"CAPTURE ERROR: {capture_error} (partial log analysed below)")
        rc = rc or 4
    res = write_report(log, arm)
    if not res["ready_marker"] and res["source"] != "ONRX":
        print("WARNING: neither [INS] ONRX_READY nor any [INS] ONRX seq= line seen (not an instrument image?)")
        rc = rc or 3
    elif not res["ready_marker"]:
        print("note: ONRX_READY not seen (boot-only marker); accepted ONRX lines instead")
    if arm == "F" and not res["txbuf_applied"]:
        print("WARNING: arm F but no txbuf=<bytes>/1 with bytes>0 in the capture; "
              "the enlarged UART TX buffer is not active (wrong image?)")
        rc = rc or 5
    print(f"log: {log}")
    return rc


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host")
    ap.add_argument("--arm", default="?", help="A|B|C|D|F (label only when --analyse)")
    ap.add_argument("--minutes", type=float)
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    ap.add_argument("--analyse", type=Path, help="analyse an existing log offline and exit")
    a = ap.parse_args(argv)
    if a.analyse:
        res = analyse(a.analyse.read_text(errors="replace").splitlines(), a.arm)
        print(format_table(res))
        print(json.dumps(res))
        return 0
    if not a.host or a.arm not in ARM_COMMANDS or not a.minutes or a.minutes <= 0:
        ap.error("--host, --arm A|B|C|D|F and --minutes > 0 are required")
    return drive(a.host, a.arm, a.minutes, a.out)


if __name__ == "__main__":
    sys.exit(main())
