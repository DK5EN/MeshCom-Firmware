#!/usr/bin/env python3
"""RX-stall bench (campaign docs/campaign-rx-stall-1229.md, W1): LoRa frame loss at a
device under test (DUT) while its web server is stalled.

Live mode (three nodes on the 2323 net console, never USB):

    rx_stall_bench.py --arm A|B|C [--dut KEY] [--sender KEY] [--ref KEY]
                      [--dut-host IP] [--sender-host IP] [--ref-host IP]
                      [--frames 60] [--interval-ms 3000] [--web-load] [--settle 10]

  1. identity guard (tools/bench/identity_guard.py) on all three nodes, abort on refusal
  2. capture DUT / reference / sender console lines to tools/bench/runs/harness/
  3. optional HTTP GET loop on http://<dut>/ (--web-load); the throttling itself
     (tools/bench/web_throttle.sh, sudo) is the operator's, the arm name is only recorded
  4. the sender gets `--loratx <n> <ms>` (src/command_functions.cpp:5979, TM-06(b)):
     n frames "LORATX <i>" to group TEST, n capped at 20 per command and ms >= 100 by the
     firmware, so 60 frames run as 3 sequential commands of 20
  5. frames are matched between DUT and reference by message id (the "xHHHHHHHH" token of
     the [LOG] line), falling back to the burst sequence number

Offline mode (no network; what the tests exercise):

    rx_stall_bench.py --analyse <dut.log> <ref.log> [--sent N] [--sender-log FILE] [--arm A]

Console lines this reads:

    [LOG] line of a received text frame   `14:22:13 [LOG] 029 @ xEA25A1F7 H04 ... DK5EN-92>TEST:LORATX 7 HW:..`
    sender markers (INSTRUMENT + MC_INJECT_HOOKS build; src/test_inject.cpp:87-93)
        [INJ];loratx;start;n;<n>;ms;<ms>
        [INJ];loratx;q;<i>;id;<msgid hex>
        [INJ];loratx;done;<queued>/<n>
    [INS] ONRX seq=.. irq_to_read_us=..     (parsed by onrx_matrix.parse_log)
    [INSTR-LOOP];gap;ms;<ms>;in;<section>;section_ms;<ms>;sections_ms;<ms>   (src/instrument.cpp:174)

Hard rules enforced in code: only group TEST, only the whitelisted console commands
(--info, --loratx n ms), no setting or TX power change, only DK5EN-* callsigns, no USB.
"""

from __future__ import annotations

import argparse
import json
import re
import socket
import sys
import threading
import time
import urllib.request
from datetime import datetime
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))

import onrx_matrix  # noqa: E402  (ONRX parser and percentile, reused unchanged)

RUNS_DIR = HERE / "runs" / "harness"
GROUP = "TEST"
CALL_PREFIX = "DK5EN-"
MAX_BURST = 20  # firmware cap per --loratx command
MIN_INTERVAL_MS = 100  # firmware floor

# ------------------------------------------------------------------ hard-rule guards


class RuleViolation(RuntimeError):
    """A command or destination the bench rules forbid."""


def require_test_group(dst: str) -> str:
    """Return ``dst`` if it is the bench group TEST, else raise."""
    if dst != GROUP:
        raise RuleViolation(f"destination '{dst}' refused: bench traffic goes to group {GROUP} only")
    return dst


def require_fleet_call(call: str) -> str:
    if not call.startswith(CALL_PREFIX):
        raise RuleViolation(f"callsign '{call}' refused: only {CALL_PREFIX}* nodes are allowed")
    return call


_CMD_OK = (
    re.compile(r"^--info$"),
    re.compile(r"^--loratx (\d+) (\d+)$"),
)


def check_command(cmd: str) -> str:
    """Whitelist for everything this harness may type into a console."""
    for rx in _CMD_OK:
        m = rx.match(cmd)
        if m:
            if rx is _CMD_OK[1]:
                n, ms = int(m.group(1)), int(m.group(2))
                if not (1 <= n <= MAX_BURST) or ms < MIN_INTERVAL_MS:
                    raise RuleViolation(f"'{cmd}' outside n 1..{MAX_BURST}, ms >= {MIN_INTERVAL_MS}")
            return cmd
    raise RuleViolation(f"console command '{cmd}' is not whitelisted")


def loratx_command(n: int, ms: int) -> str:
    """The sender burst command; destination is fixed to TEST by the firmware."""
    return check_command(f"--loratx {n} {ms}")


# ------------------------------------------------------------------ parsing

_RX_FRAME = re.compile(
    r"\[LOG\].*?"
    r"(?P<src>[A-Z0-9]+-\d+)(?:,[^>\s]*)?>(?P<dst>[^:\s;]+):LORATX (?P<seq>\d+)"
)
_RX_ID = re.compile(r"\bx([0-9A-Fa-f]{8})\b")
_SND_Q = re.compile(r"\[INJ\];loratx;q;(\d+);id;([0-9A-Fa-f]{8})")
_SND_DONE = re.compile(r"\[INJ\];loratx;done;(\d+)/(\d+)")
# printfdeb() turns every ';' into a space on the console, so both separators occur
_LOOP = re.compile(r"\[INSTR-LOOP\][; ]gap[; ]ms[; ](\d+)[; ]in[; ](\S+?)[; ]section_ms[; ](\d+)[; ]sections_ms[; ](\d+)")


def parse_frames(lines, sender_call: str | None = None) -> dict[str, dict]:
    """Received LORATX frames addressed to TEST, keyed by msgid (or ``seq<n>``).

    Frames to any other destination are ignored; relay copies collapse onto one key.
    """
    out: dict[str, dict] = {}
    for line in lines:
        if "LORATX" not in line:
            continue
        m = _RX_FRAME.search(line)
        if not m:
            continue
        if m.group("dst") != GROUP:
            continue
        if sender_call and m.group("src") != sender_call:
            continue
        mid = _RX_ID.search(line)
        key = mid.group(1).upper() if mid else f"seq{int(m.group('seq'))}"
        out.setdefault(key, {"seq": int(m.group("seq")), "src": m.group("src"), "n": 0})["n"] += 1
    return out


_SND_TX = re.compile(r"\[LOG\] TX x([0-9A-Fa-f]{8})\b")


def parse_sender(lines) -> dict[str, int]:
    """Burst frames the sender actually transmitted: {msgid: burst index}.

    `[INJ];loratx;q` only says the frame entered the TX ring; under a busy channel the ring
    lags behind the burst interval and frames still queued at the end of the capture never went
    on air. When the sender log carries `[LOG] TX x<id>` lines, only those ids count.
    """
    lines = list(lines)
    queued: dict[str, int] = {}
    sent_ids: set[str] = set()
    for line in lines:
        m = _SND_Q.search(line)
        if m:
            queued[m.group(2).upper()] = int(m.group(1))
        t = _SND_TX.search(line)
        if t:
            sent_ids.add(t.group(1).upper())
    if sent_ids:
        return {k: v for k, v in queued.items() if k in sent_ids}
    return queued


def parse_loop_gaps(lines) -> dict[str, dict]:
    """[INSTR-LOOP] gaps grouped by section: count, max and total gap ms."""
    out: dict[str, dict] = {}
    for line in lines:
        m = _LOOP.search(line)
        if not m:
            continue
        gap, sect = int(m.group(1)), m.group(2)
        g = out.setdefault(sect, {"count": 0, "max_ms": 0, "total_ms": 0})
        g["count"] += 1
        g["total_ms"] += gap
        g["max_ms"] = max(g["max_ms"], gap)
    return out


def onrx_stats(lines) -> dict:
    vals = [r["irq_to_read_us"] for r in onrx_matrix.parse_log(lines)["onrx"] if "irq_to_read_us" in r]
    return {
        "n": len(vals),
        "irq_to_read_us_p50": onrx_matrix.pct(vals, 50),
        "irq_to_read_us_max": float(max(vals)) if vals else None,
    }


def analyse(dut_lines, ref_lines, sent: int | None = None, sender_lines=None,
            sender_call: str | None = None, arm: str | None = None) -> dict:
    """Summary of one arm from saved logs."""
    dut_lines, ref_lines = list(dut_lines), list(ref_lines)
    dut = parse_frames(dut_lines, sender_call)
    ref = parse_frames(ref_lines, sender_call)
    queued = parse_sender(sender_lines) if sender_lines is not None else {}

    if queued:
        universe = set(queued)
        if sent is None:
            sent = len(queued)
    else:
        universe = set(dut) | set(ref)
        if sent is None:
            sent = len(universe)
    # With a known id set only those ids count; otherwise everything seen counts.
    seen_dut = set(dut) & universe if queued else set(dut)
    seen_ref = set(ref) & universe if queued else set(ref)
    seen_any = seen_dut | seen_ref
    return {
        "arm": arm,
        "frames_sent": sent,
        "seen_dut": len(seen_dut),
        "seen_ref": len(seen_ref),
        "lost_dut_seen_ref": len(seen_ref - seen_dut),
        "seen_dut_not_ref": len(seen_dut - seen_ref),
        "lost_both": max(sent - len(seen_any), 0),
        "dut_loss_pct": round(100.0 * (sent - len(seen_dut)) / sent, 1) if sent else None,
        "ref_loss_pct": round(100.0 * (sent - len(seen_ref)) / sent, 1) if sent else None,
        "onrx_dut": onrx_stats(dut_lines),
        "instr_loop_dut": parse_loop_gaps(dut_lines),
    }


def format_summary(s: dict) -> str:
    on = s["onrx_dut"]
    rows = [
        f"arm {s['arm']}: sent {s['frames_sent']}, DUT {s['seen_dut']}, ref {s['seen_ref']}, "
        f"lost at DUT but seen at ref {s['lost_dut_seen_ref']}, lost at both {s['lost_both']}",
        f"  loss DUT {s['dut_loss_pct']} %  ref {s['ref_loss_pct']} %",
        f"  irq_to_read_us n={on['n']} p50={on['irq_to_read_us_p50']} max={on['irq_to_read_us_max']}",
    ]
    for sect, g in sorted(s["instr_loop_dut"].items(), key=lambda kv: -kv[1]["max_ms"]):
        rows.append(f"  INSTR-LOOP {sect}: {g['count']} gaps, max {g['max_ms']} ms, total {g['total_ms']} ms")
    if not s["instr_loop_dut"]:
        rows.append("  INSTR-LOOP: no gaps")
    return "\n".join(rows)


# ------------------------------------------------------------------ live mode


class NodeCapture:
    """One 2323 console client: login, line capture to a file and memory."""

    def __init__(self, name: str, host: str, call: str, path: Path) -> None:
        import mc_console

        self.name, self.host, self.call, self.path = name, host, call, path
        self.lines: list[str] = []
        self._con = mc_console.Console(host, call=call)
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None
        self._lock = threading.Lock()

    def start(self) -> None:
        rest = self._con.connect()
        self._fh = self.path.open("w", encoding="utf-8", errors="replace")
        self._feed(rest.decode("utf-8", "replace") if rest else "")
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def _feed(self, text: str) -> None:
        if not text:
            return
        self._buf = getattr(self, "_buf", "") + text
        *whole, self._buf = self._buf.split("\n")
        for ln in whole:
            ln = ln.rstrip("\r")
            stamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
            self._fh.write(f"{stamp} {ln}\n")
            with self._lock:
                self.lines.append(ln)
        self._fh.flush()

    def _run(self) -> None:
        sock = self._con.sock
        while not self._stop.is_set() and sock is not None:
            try:
                d = sock.recv(4096)
            except socket.timeout:
                continue
            except OSError:
                break
            if not d:
                break
            self._feed(d.decode("utf-8", "replace"))

    def send(self, cmd: str) -> None:
        self._con.send(check_command(cmd))

    def snapshot(self) -> list[str]:
        with self._lock:
            return list(self.lines)

    def stop(self) -> None:
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=2)
        self._con.close()
        if getattr(self, "_fh", None):
            self._fh.close()


class WebLoad:
    """GET / on the DUT in a loop until stopped; records count, errors, worst latency."""

    def __init__(self, host: str, timeout: float = 20.0) -> None:
        self.url, self.timeout = f"http://{host}/", timeout
        self.ok = self.err = 0
        self.max_s = 0.0
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)

    def _run(self) -> None:
        while not self._stop.is_set():
            t0 = time.monotonic()
            try:
                with urllib.request.urlopen(self.url, timeout=self.timeout) as r:  # noqa: S310
                    r.read()
                self.ok += 1
            except Exception:
                self.err += 1
            self.max_s = max(self.max_s, time.monotonic() - t0)
            self._stop.wait(0.2)

    def start(self) -> None:
        self._thread.start()

    def stop(self) -> dict:
        self._stop.set()
        self._thread.join(timeout=self.timeout + 2)
        return {"requests_ok": self.ok, "requests_err": self.err, "max_latency_s": round(self.max_s, 2)}


def resolve(key: str, host: str | None, fleet: dict) -> tuple[str, str, str]:
    """(fleet key, host, callsign) for a node; host override wins over fleet.json."""
    entry = fleet["nodes"].get(key)
    if entry is None:
        raise RuleViolation(f"node '{key}' is not in fleet.json")
    use_host = host or entry.get("host")
    if not use_host:
        raise RuleViolation(f"node '{key}' has no net-console host: pass --{key}-host style override")
    return key, use_host, require_fleet_call(entry["call"])


def guard_all(nodes: list[tuple[str, str, str]], fleet: dict) -> None:
    """Identity guard on every node; raise on the first refusal."""
    import identity_guard

    for key, host, call in nodes:
        text = identity_guard.read_info_net(host, call=call)
        info = identity_guard.require(text, key, fleet)
        print(f"identity guard: {key} ok -- {info.call}, {info.txpower_dbm} dBm @ {host}")


def wait_done(sender: NodeCapture, since: int, timeout: float) -> bool:
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if any(_SND_DONE.search(ln) or "loratx;err" in ln for ln in sender.snapshot()[since:]):
            return True
        time.sleep(0.5)
    return False


def run_live(args: argparse.Namespace) -> int:
    import identity_guard

    fleet = identity_guard.load_fleet()
    try:
        dut = resolve(args.dut, args.dut_host, fleet)
        snd = resolve(args.sender, args.sender_host, fleet)
        ref = resolve(args.ref, args.ref_host, fleet)
        if len({dut[0], snd[0], ref[0]}) != 3:
            raise RuleViolation("DUT, sender and reference must be three different nodes")
        guard_all([dut, snd, ref], fleet)
    except (RuleViolation, identity_guard.IdentityError, OSError) as exc:
        print(f"abort: {exc}", file=sys.stderr)
        return 1

    RUNS_DIR.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    base = RUNS_DIR / f"rx_stall_{args.arm}_{stamp}"
    caps = {
        "dut": NodeCapture("dut", dut[1], dut[2], Path(f"{base}_dut.log")),
        "ref": NodeCapture("ref", ref[1], ref[2], Path(f"{base}_ref.log")),
        "sender": NodeCapture("sender", snd[1], snd[2], Path(f"{base}_sender.log")),
    }
    web: WebLoad | None = None
    web_res: dict | None = None
    try:
        for c in caps.values():
            c.start()
        if args.web_load:
            web = WebLoad(dut[1])
            web.start()
        left, queued_cmds = args.frames, 0
        while left > 0:
            n = min(left, MAX_BURST)
            mark = len(caps["sender"].snapshot())
            caps["sender"].send(loratx_command(n, args.interval_ms))
            queued_cmds += 1
            if not wait_done(caps["sender"], mark, n * args.interval_ms / 1000.0 + 30):
                print("warning: no done marker from the sender", file=sys.stderr)
            left -= n
        time.sleep(args.settle)
    finally:
        if web:
            web_res = web.stop()
        for c in caps.values():
            c.stop()

    summary = analyse(
        caps["dut"].lines, caps["ref"].lines, sender_lines=caps["sender"].lines,
        sender_call=snd[2], arm=args.arm,
    )
    summary.update({"nodes": {"dut": dut[2], "sender": snd[2], "ref": ref[2]},
                    "interval_ms": args.interval_ms, "web_load": web_res, "stamp": stamp})
    out = Path(f"{base}.json")
    out.write_text(json.dumps(summary, indent=2) + "\n")
    print(format_summary(summary))
    print(f"summary: {out}")
    return 0


def run_offline(args: argparse.Namespace) -> int:
    dut_log, ref_log = (Path(p) for p in args.analyse)
    sender_lines = Path(args.sender_log).read_text(errors="replace").splitlines() if args.sender_log else None
    summary = analyse(
        dut_log.read_text(errors="replace").splitlines(),
        ref_log.read_text(errors="replace").splitlines(),
        sent=args.sent, sender_lines=sender_lines, sender_call=args.sender_call, arm=args.arm,
    )
    if args.json:
        print(json.dumps(summary, indent=2))
    else:
        print(format_summary(summary))
    return 0


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--arm", default=None, choices=["A", "B", "C"], help="arm name, recorded only")
    ap.add_argument("--analyse", nargs=2, metavar=("DUT_LOG", "REF_LOG"), help="offline analysis of saved logs")
    ap.add_argument("--sent", type=int, default=None, help="offline: frames sent (default: ids seen)")
    ap.add_argument("--sender-log", default=None, help="offline: sender console log (queued ids)")
    ap.add_argument("--sender-call", default=None, help="offline: only count frames from this callsign")
    ap.add_argument("--json", action="store_true", help="offline: print JSON instead of text")
    ap.add_argument("--dut", default="heltec-1", help="fleet key of the device under test")
    ap.add_argument("--sender", default="t-beam-92", help="fleet key of the burst sender")
    ap.add_argument("--ref", default="t-deck-14", help="fleet key of the reference receiver")
    ap.add_argument("--dut-host", default=None)
    ap.add_argument("--sender-host", default=None)
    ap.add_argument("--ref-host", default=None)
    ap.add_argument("--frames", type=int, default=60)
    ap.add_argument("--interval-ms", type=int, default=3000)
    ap.add_argument("--settle", type=float, default=10.0, help="seconds to keep capturing after the last frame")
    ap.add_argument("--web-load", action="store_true", help="GET / on the DUT in a loop during the burst")
    return ap


def main(argv: list[str] | None = None) -> int:
    ap = build_parser()
    args = ap.parse_args(argv)
    if args.analyse:
        return run_offline(args)
    if not args.arm:
        ap.error("--arm A|B|C is required in live mode")
    if args.frames < 1 or args.interval_ms < MIN_INTERVAL_MS:
        ap.error(f"--frames >= 1 and --interval-ms >= {MIN_INTERVAL_MS} required")
    return run_live(args)


if __name__ == "__main__":
    sys.exit(main())
