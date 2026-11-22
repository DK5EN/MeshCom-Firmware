#!/usr/bin/env python3
"""OTA-LOWHEAP-01 overnight trigger: run Auto Update check + install on aged ESP32 nodes.

Per node (in the order given): take the console from the running meshlogger
(PAUSE file in its outdir), log --info and the current heap, then up to
`--attempts` rounds of  --update check -> wait -> --update install -> wait ->
--update status -> --update forget, `--gap` seconds apart.  "install" only
downloads and stages; this tool never sends "--update apply" (hard-coded
whitelist below).  --dry-run only reads (--info, --autoupdate status,
--update status, one heap sample).

Outputs under --outdir:
  trigger-<call>.log   every console line of the session (timestamped)
  trigger.summary      one line per attempt: call attempt uptime free blk verdict

Usage:
  ota_lowheap_trigger.py --target DK5EN-1=192.168.68.78 --target DK5EN-92=... [--dry-run]
"""
from __future__ import annotations

import argparse
import re
import socket
import sys
import time
from datetime import datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import mc_console  # noqa: E402

DEFAULT_OUT = Path(__file__).resolve().parent / "runs/harness/ota-lowheap-night-20261122"

# Everything the tool may send. No apply, no autoupdate setter, no gateway/config.
ALLOWED = {
    "--info", "--autoupdate status", "--update status", "--update check",
    "--update install", "--update forget", "--heaplog 1", "--heaplog 60",
}


class Session:
    def __init__(self, call: str, host: str, logpath: Path) -> None:
        self.call, self.host = call, host
        self.log = open(logpath, "a", buffering=1)
        self.con = mc_console.Console(host, call=call)
        self.lines: list[str] = []

    def w(self, line: str) -> None:
        self.log.write(f"{datetime.now().strftime('%Y-%m-%d %H:%M:%S')} {line}\n")

    def connect(self, tries: int = 12) -> None:
        err: Exception | None = None
        for _ in range(tries):
            try:
                rest = self.con.connect()
                self.w(f"[TRIG] connected {self.host}")
                self._ingest(rest)
                return
            except mc_console.LoginError:
                raise
            except OSError as e:
                err = e
                time.sleep(5)
        raise ConnectionError(f"{self.call}: console not reachable: {err}")

    def _ingest(self, data: bytes) -> list[str]:
        out = []
        for ln in data.decode(errors="replace").replace("\r", "").split("\n"):
            if ln.strip():
                self.w(ln)
                out.append(ln)
                self.lines.append(ln)
        return out

    def drain(self, seconds: float) -> list[str]:
        """Read for `seconds`, return the lines."""
        assert self.con.sock is not None
        buf = b""
        end = time.monotonic() + seconds
        self.con.sock.settimeout(0.3)
        while time.monotonic() < end:
            try:
                d = self.con.sock.recv(4096)
            except socket.timeout:
                continue
            if not d:
                raise ConnectionError("console closed")
            buf += d
        return self._ingest(buf)

    def cmd(self, c: str, wait: float = 4.0) -> list[str]:
        if c not in ALLOWED:
            raise RuntimeError(f"command not allowed: {c}")
        self.w(f"[TRIG] >>> {c}")
        self.con.send(c)
        return self.drain(wait)

    def close(self) -> None:
        self.con.close()
        self.log.close()


def heap_sample(s: Session) -> dict:
    """One [INSTR-HEAP] sample (heaplog 1 for 3 s, then back to 60 s)."""
    ls = s.cmd("--heaplog 1", 3.5)
    s.cmd("--heaplog 60", 1.5)
    for ln in reversed(ls):
        if re.search(r"\[INSTR-HEAP\][; ]up_s", ln):
            kv = ln.split("[INSTR-HEAP]", 1)[1].strip(" ;").replace(";", " ").split()
            d = dict(zip(kv[0::2], kv[1::2]))
            return {"up": d.get("up_s", "?"), "free": d.get("ifree", "?"), "blk": d.get("iblk", "?")}
    return {"up": "?", "free": "?", "blk": "?"}


def status_busy(lines: list[str]) -> int | None:
    for ln in reversed(lines):
        m = re.search(r"\[AU\];status;.*;busy;(\d);", ln)
        if m:
            return int(m.group(1))
    return None


def wait_done(s: Session, limit: float, started: float) -> list[str]:
    """Poll --update status every 10 s until busy;0 (after a min. grace) or limit."""
    got: list[str] = []
    while time.monotonic() - started < limit:
        got += s.drain(10)
        ls = s.cmd("--update status", 2.0)
        got += ls
        if status_busy(ls) == 0 and time.monotonic() - started > 12:
            return got
    s.w(f"[TRIG] wait limit {limit}s reached")
    return got


def verdict(lines: list[str]) -> str:
    txt = "\n".join(lines)
    if "[AU];refuse;lowheap" in txt or ";err;lowheap" in txt:
        return "refused"
    for ln in reversed(lines):
        m = re.search(r"\[AU\];status;.*;staged;([^;]+);", ln)
        if m and m.group(1) != "none":
            return "staged"
    if "[AU];install;already_staged" in txt:
        return "staged"
    return "other"


def summary(outdir: Path, call: str, n: int, hs: dict, v: str, extra: str = "") -> None:
    with open(outdir / "trigger.summary", "a") as f:
        f.write(f"{datetime.now().strftime('%Y-%m-%d %H:%M:%S')} {call} attempt={n} "
                f"uptime_s={hs['up']} ifree={hs['free']} iblk={hs['blk']} verdict={v}{extra}\n")


def run_node(call: str, host: str, a: argparse.Namespace) -> None:
    outdir: Path = a.outdir
    pause = outdir / call.lower() / "PAUSE"
    if a.release_logger:
        pause.parent.mkdir(parents=True, exist_ok=True)
        pause.touch()
        time.sleep(15)  # logger releases the single-client console
    s = Session(call, host, outdir / f"trigger-{call}.log")
    try:
        s.connect()
        s.cmd("--info", 5.0)
        s.cmd("--autoupdate status", 2.0)
        s.cmd("--update status", 2.0)
        hs = heap_sample(s)
        s.w(f"[TRIG] heap sample {hs}")
        if a.dry_run:
            summary(outdir, call, 0, hs, "dryrun")
            return
        for n in range(1, a.attempts + 1):
            if n > 1:
                s.drain(a.gap)
            hs = heap_sample(s)
            s.w(f"[TRIG] attempt {n} start, heap {hs}")
            t0 = time.monotonic()
            got = s.cmd("--update check", 3.0)
            got += wait_done(s, 180, t0)
            t1 = time.monotonic()
            got += s.cmd("--update install", 3.0)
            got += wait_done(s, 600, t1)
            got += s.cmd("--update status", 2.0)
            v = verdict(got)
            summary(outdir, call, n, hs, v)
            s.cmd("--update forget", 2.0)
    finally:
        s.close()
        if a.release_logger:
            try:
                pause.unlink()
            except FileNotFoundError:
                pass


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--target", action="append", required=True, metavar="CALL=HOST")
    ap.add_argument("--outdir", type=Path, default=DEFAULT_OUT)
    ap.add_argument("--attempts", type=int, default=3)
    ap.add_argument("--gap", type=float, default=120.0, help="seconds between attempts")
    ap.add_argument("--dry-run", action="store_true", help="read-only: --info, status, one heap sample")
    ap.add_argument("--no-release-logger", dest="release_logger", action="store_false",
                    help="do not touch the meshlogger PAUSE file (logger not running)")
    a = ap.parse_args()
    a.outdir.mkdir(parents=True, exist_ok=True)
    rc = 0
    for t in a.target:
        call, host = t.split("=", 1)
        if call.upper() in ("DK5EN-98", "DK5EN-90"):
            print(f"refusing {call}", file=sys.stderr)
            rc = 2
            continue
        try:
            run_node(call.upper(), host, a)
        except Exception as e:  # one node failing must not stop the others
            with open(a.outdir / "trigger.summary", "a") as f:
                f.write(f"{datetime.now():%Y-%m-%d %H:%M:%S} {call} ERROR {type(e).__name__}: {e}\n")
            rc = 1
    return rc


if __name__ == "__main__":
    sys.exit(main())
