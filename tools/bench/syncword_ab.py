#!/usr/bin/env python3
"""Sync-word A/B run on the bench fleet (SYNC-NRF-01, campaign RC-BENCH 2026-11-25).

One run = one image on the RAK4631 (label A = old 0x242B, B = new 0x24B4). Every node sends ``--count``
group messages to group 9999 in turn through the node's web API (``/?sendmessage&tocall=9999``; never a
broadcast, never another destination); all other nodes listen.
Per sender -> receiver pair the run counts what the sender put on air (its own ``TX x<id> ... src=o`` lines),
how many of those ids the receiver decoded DIRECTLY (path ``SENDER>`` without a relay), the receiver's ERR lines
(CRC/RX errors, ``--setlog``) and other RX errors inside the sender's window, and RSSI/SNR of the direct copies.

    syncword_ab.py --label B --rak-port /dev/cu.usbmodem2101 --rak-host 192.168.68.70 \\
        --esp DK5EN-1=192.168.68.69 --esp DK5EN-14=192.168.68.77 --esp DK5EN-92=192.168.68.61 \\
        --count 20 --spacing 12 --out tools/bench/runs/harness/rc-bench-20261125/syncword
    syncword_ab.py --label B-low ... --txpower -9     # every sender at -9 dBm for this run, restored after
    syncword_ab.py --analyse DIR                      # recompute the tables from the captured logs
    syncword_ab.py --self-test

Safety: identity guard on every node first (fleet.json, own callsign, <= 2 dBm); text only to group 9999;
``--setlog on`` for the run and the previous state restored (also on error); ``--txpower`` restored per node.
Exit 0 = run done (the verdict is in the tables, not in the exit code), 1 = guard refused or a node failed,
2 = usage.
"""
from __future__ import annotations

import argparse
import json
import re
import socket
import statistics
import sys
import threading
import time
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Dict, List, Optional, Tuple

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))

GROUP = "9999"
RAK_CALL = "DK5EN-90"

# "[LOG] 029 @ xEA25A2BA H04 S0 T0 M01 DK5EN-1>HG@R2; ... RSSI:-43 SNR:6 DUP:n OWN:- t=128147"
_RX = re.compile(r"\[LOG\]\s+\d+\s+\S\s+x([0-9A-F]{8})\s.*?\s([A-Z0-9]+-\d+(?:,[A-Z0-9-]+)*)>"
                 r".*?RSSI:(-?\d+)\s+SNR:(-?\d+)\s+DUP:([nd])")
# "[LOG] TX xB597812D : H02 prio=5 src=o wait=15337 q=1 cad=2 len=54 t=1236982"
_TX = re.compile(r"\[LOG\]\s+TX\s+x([0-9A-F]{8})\s+(\S)\s.*?src=o")
_ERR = re.compile(r"\[LOG\]\s+ERR\s+rssi=(-?\d+)\s+snr=(-?\d+)")
_OTHER = re.compile(r"RX_OTHER_ERROR|RX_ERROR rssi")
_SETLOG = re.compile(r"DisplyLog\s+(on|off)")
_TXPWR = re.compile(r"TXPWR (-?\d+) dBm")  # --info, as identity_guard reads it


# ---------------------------------------------------------------- pure logic

def parse_rx(line: str) -> Optional[Tuple[str, str, bool, int, int, bool]]:
    """(msg id, originator, direct, rssi, snr, dup) of an RX [LOG] line, else None."""
    m = _RX.search(line)
    if not m:
        return None
    path = m.group(2).split(",")
    return m.group(1), path[0], len(path) == 1, int(m.group(3)), int(m.group(4)), m.group(5) == "d"


def parse_tx(line: str) -> Optional[str]:
    """Message id of an own transmission (src=o), else None."""
    m = _TX.search(line)
    return m.group(1) if m else None


def is_err(line: str) -> bool:
    return bool(_ERR.search(line) or _OTHER.search(line))


def pair_table(logs: Dict[str, List[Tuple[float, str]]],
               windows: Dict[str, Tuple[float, float]]) -> List[dict]:
    """One row per (sender, receiver). logs: call -> [(epoch, line)], windows: sender -> (start, end)."""
    rows = []
    for snd, (t0, t1) in windows.items():
        sent = {i for t, l in logs.get(snd, []) if t0 <= t <= t1 for i in [parse_tx(l)] if i}
        for rcv, lines in logs.items():
            if rcv == snd:
                continue
            got: Dict[str, Tuple[int, int]] = {}
            relayed = set()
            errs = 0
            for t, l in lines:
                if not (t0 <= t <= t1 + 30):
                    continue
                r = parse_rx(l)
                if r:
                    mid, orig, direct, rssi, snr, _dup = r
                    if mid in sent and orig == snd:
                        if direct:
                            got.setdefault(mid, (rssi, snr))
                        else:
                            relayed.add(mid)
                elif t <= t1 and is_err(l):
                    errs += 1
            rs = [v[0] for v in got.values()]
            ss = [v[1] for v in got.values()]
            rows.append({
                "sender": snd, "receiver": rcv, "sent": len(sent), "direct": len(got),
                "relay_only": len(relayed - set(got)), "err_in_window": errs,
                "rssi_med": statistics.median(rs) if rs else None, "rssi_min": min(rs) if rs else None,
                "snr_med": statistics.median(ss) if ss else None, "snr_min": min(ss) if ss else None,
            })
    return rows


def render(rows: List[dict], label: str) -> str:
    out = [f"### Run {label}", "",
           "| Sender | Receiver | Sent | Decoded direct | Relay only | Errors in window | RSSI med/min | SNR med/min |",
           "| ------ | -------- | ---- | -------------- | ---------- | ---------------- | ------------ | ----------- |"]
    for r in rows:
        def mm(a, b):
            return "-" if a is None else f"{a:g} / {b:g}"
        out.append(f"| {r['sender']} | {r['receiver']} | {r['sent']} | {r['direct']} | {r['relay_only']} | "
                   f"{r['err_in_window']} | {mm(r['rssi_med'], r['rssi_min'])} | {mm(r['snr_med'], r['snr_min'])} |")
    return "\n".join(out) + "\n"


# ---------------------------------------------------------------- I/O

class Capture:
    """Reads one node's console into (epoch, line) pairs on a thread; send() writes a command."""

    def __init__(self, call: str, logf: Path):
        self.call = call
        self.lines: List[Tuple[float, str]] = []
        self.logf = open(logf, "a", buffering=1, encoding="utf-8")
        self.stop = threading.Event()
        self._partial = ""
        self.thread: Optional[threading.Thread] = None

    def _feed(self, data: bytes) -> None:
        text = self._partial + data.decode("utf-8", errors="replace")
        parts = text.split("\n")
        self._partial = parts.pop()
        now = time.time()
        for ln in parts:
            ln = ln.rstrip("\r")
            self.lines.append((now, ln))
            self.logf.write(f"{now:.3f} {ln}\n")

    def since(self, idx: int) -> str:
        return "\n".join(ln for _, ln in self.lines[idx:])


class NetCapture(Capture):
    def __init__(self, call: str, host: str, logf: Path):
        super().__init__(call, logf)
        from mc_console import Console
        self.con = Console(host, call=call)

    def start(self) -> None:
        self._feed(self.con.connect())
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()

    def _run(self) -> None:
        assert self.con.sock is not None
        self.con.sock.settimeout(0.3)
        while not self.stop.is_set():
            try:
                d = self.con.sock.recv(4096)
            except socket.timeout:
                continue
            except OSError:
                break
            if not d:
                break
            self._feed(d)

    def send(self, cmd: str) -> None:
        self.logf.write(f"{time.time():.3f} >> {cmd}\n")
        self.con.send(cmd)

    def close(self) -> None:
        self.stop.set()
        if self.thread:
            self.thread.join(2)
        self.con.close()


class SerialCapture(Capture):
    def __init__(self, call: str, port: str, logf: Path):
        super().__init__(call, logf)
        import serial  # noqa: F401  (pyserial)
        from rak_harness import dtr_for
        self.port, self.dtr = port, dtr_for(port)
        self.ser = None

    def start(self) -> None:
        import serial
        s = serial.Serial()
        s.port, s.baudrate, s.timeout, s.dtr, s.rts = self.port, 115200, 0.2, self.dtr, False
        s.open()
        self.ser = s
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()

    def _run(self) -> None:
        while not self.stop.is_set():
            try:
                d = self.ser.read(4096)
            except Exception:  # noqa: BLE001 -- port gone: stop the capture, the run reports the node
                break
            if d:
                self._feed(d)

    def send(self, cmd: str) -> None:
        self.logf.write(f"{time.time():.3f} >> {cmd}\n")
        self.ser.write((cmd + "\n").encode())
        self.ser.flush()

    def close(self) -> None:
        self.stop.set()
        if self.thread:
            self.thread.join(2)
        if self.ser:
            self.ser.close()


def send_group(host: str, text: str, dest: str = GROUP) -> str:
    """One group message through the node's web API (soak_dm.py path). Only group 9999: an empty or other
    destination would go out as a broadcast or to a foreign station, so it is refused here."""
    if dest != GROUP or not text:
        raise ValueError(f"refused: destination {dest!r}, only group {GROUP}")
    q = "sendmessage&tocall=" + urllib.parse.quote(dest) + "&message=" + urllib.parse.quote(text)
    req = urllib.request.Request(f"http://{host}/?{q}", headers={"X-MC": "1"})
    with urllib.request.urlopen(req, timeout=15) as r:
        body = r.read(300).decode("utf-8", "replace")
    return "ok" if "sendmessage ok" in body else body.strip()[:80]


def ask(cap: Capture, cmd: str, wait: float = 4.0) -> str:
    i = len(cap.lines)
    cap.send(cmd)
    time.sleep(wait)
    return cap.since(i)


NODE_OF = {"DK5EN-1": "heltec-1", "DK5EN-14": "t-deck-14", "DK5EN-92": "t-beam-92", "DK5EN-90": "rak-90"}


def run(args: argparse.Namespace) -> int:
    from identity_guard import require, IdentityError
    out = Path(args.out) / args.label
    out.mkdir(parents=True, exist_ok=True)
    caps: Dict[str, Capture] = {}
    restore: Dict[str, List[str]] = {}
    try:
        caps[RAK_CALL] = SerialCapture(RAK_CALL, args.rak_port, out / f"{RAK_CALL}.log")
        hosts = {RAK_CALL: args.rak_host}
        for spec in args.esp:
            call, host = spec.split("=", 1)
            hosts[call] = host
            caps[call] = NetCapture(call, host, out / f"{call}.log")
        for c in caps.values():
            c.start()
        time.sleep(2)
        for call, c in caps.items():
            info = ask(c, "--info", 5)
            try:
                require(info, NODE_OF[call], offline_ok=(call == RAK_CALL))
            except IdentityError as e:
                print(f"{call}: identity guard refused: {e}", file=sys.stderr)
                return 1
            m = _SETLOG.search(info)
            restore[call] = [f"--setlog {m.group(1) if m else 'off'}"]
            if args.txpower is not None:
                p = _TXPWR.search(info)
                if not p:
                    print(f"{call}: TX power not found in --info, refusing --txpower", file=sys.stderr)
                    return 1
                restore[call].append(f"--txpower {p.group(1)}")
                ask(c, f"--txpower {args.txpower}", 2)
            ask(c, "--setlog on", 2)
        windows: Dict[str, Tuple[float, float]] = {}
        order = [RAK_CALL] + [s.split("=", 1)[0] for s in args.esp]
        for snd in order:
            t0 = time.time()
            for i in range(args.count):
                text = f"SWAB {args.label} {snd} {i + 1}/{args.count}"
                caps[snd].logf.write(f"{time.time():.3f} >> web sendmessage {{{GROUP}}}{text}\n")
                try:
                    res = send_group(hosts[snd], text)
                except OSError as e:
                    res = f"error {e}"
                caps[snd].logf.write(f"{time.time():.3f} << web {res}\n")
                time.sleep(args.spacing)
            time.sleep(args.tail)
            windows[snd] = (t0, time.time())
            print(f"{snd}: window {windows[snd][1] - t0:.0f} s", flush=True)
        (out / "windows.json").write_text(json.dumps(windows, indent=1))
    finally:
        for call, cmds in restore.items():
            for cmd in cmds:
                try:
                    ask(caps[call], cmd, 2)
                except Exception as e:  # noqa: BLE001 -- report and continue restoring the others
                    print(f"{call}: restore '{cmd}' failed: {e}", file=sys.stderr)
        for c in caps.values():
            c.close()
    return analyse(out, args.label)


def load_logs(d: Path) -> Dict[str, List[Tuple[float, str]]]:
    logs = {}
    for f in d.glob("DK5EN-*.log"):
        rows = []
        for ln in f.read_text(encoding="utf-8", errors="replace").splitlines():
            ts, _, rest = ln.partition(" ")
            try:
                rows.append((float(ts), rest))
            except ValueError:
                continue
        logs[f.stem] = rows
    return logs


def analyse(d: Path, label: Optional[str] = None) -> int:
    windows = {k: tuple(v) for k, v in json.loads((d / "windows.json").read_text()).items()}
    rows = pair_table(load_logs(d), windows)
    (d / "pairs.json").write_text(json.dumps(rows, indent=1))
    md = render(rows, label or d.name)
    (d / "pairs.md").write_text(md)
    print(md)
    return 0


def self_test() -> int:
    rx = "12:00:01 [LOG] 029 : xEA25A2BA H02 S0 T0 M01 DK5EN-90>{9999}SWAB; HW:43 RSSI:-43 SNR:6 DUP:n OWN:- t=1"
    rly = "12:00:02 [LOG] 029 : xEA25A2BA H01 S0 T0 M01 DK5EN-90,DK5EN-1>{9999}SWAB; RSSI:-50 SNR:5 DUP:d OWN:- t=2"
    tx = "12:00:00 [LOG] TX xEA25A2BA : H02 prio=5 src=o wait=10 q=1 cad=0 len=54 t=0"
    txr = "12:00:00 [LOG] TX xEA25A2BB : H01 prio=5 src=r wait=10 q=1 cad=0 len=54 t=0"
    assert parse_rx(rx) == ("EA25A2BA", "DK5EN-90", True, -43, 6, False), parse_rx(rx)
    assert parse_rx(rly)[1:3] == ("DK5EN-90", False)
    assert parse_tx(tx) == "EA25A2BA" and parse_tx(txr) is None
    for bad in ("", "9", "DK5EN-1", "*"):
        try:
            send_group("127.0.0.1", "x", bad)
        except ValueError:
            continue
        raise AssertionError(f"destination {bad!r} not refused")
    assert is_err("[LOG] ERR rssi=-46 snr=0 len=68 ferr=-1987 t=1") and not is_err(rx)
    logs = {"DK5EN-90": [(10.0, tx)], "DK5EN-1": [(11.0, rx), (12.0, "[LOG] ERR rssi=-1 snr=0 len=1 ferr=0 t=1")],
            "DK5EN-92": [(11.0, rly)]}
    rows = {(r["sender"], r["receiver"]): r for r in pair_table(logs, {"DK5EN-90": (9.0, 20.0)})}
    a, b = rows[("DK5EN-90", "DK5EN-1")], rows[("DK5EN-90", "DK5EN-92")]
    assert (a["sent"], a["direct"], a["err_in_window"], a["rssi_med"]) == (1, 1, 1, -43), a
    assert (b["direct"], b["relay_only"]) == (0, 1), b
    print("syncword_ab self-test ok")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--label")
    ap.add_argument("--rak-port")
    ap.add_argument("--rak-host", help="web address of DK5EN-90 (sends go through the web API)")
    ap.add_argument("--esp", action="append", default=[], metavar="CALL=HOST")
    ap.add_argument("--count", type=int, default=20)
    ap.add_argument("--spacing", type=float, default=12.0)
    ap.add_argument("--tail", type=float, default=20.0)
    ap.add_argument("--txpower", type=int, default=None)
    ap.add_argument("--out", default="tools/bench/runs/harness/rc-bench-20261125/syncword")
    ap.add_argument("--analyse", metavar="DIR")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    if a.analyse:
        return analyse(Path(a.analyse))
    if not (a.label and a.rak_port and a.rak_host and a.esp):
        ap.error("--label, --rak-port, --rak-host and at least one --esp are required")
    if a.txpower is not None and a.txpower > 2:
        ap.error("bench TX power is at most 2 dBm")
    return run(a)


if __name__ == "__main__":
    sys.exit(main())
