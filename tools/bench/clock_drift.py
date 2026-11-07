#!/usr/bin/env python3
"""CLK-01 host tool: node clock drift, clock jumps and display-clock gaps over USB serial.

Opens up to three serial ports in parallel (resolved by USB serial number from
tools/bench/fleet.json, like bench_suite.py), stamps every line with host time,
parses [CLOCK] / [DISPCLK] lines, decodes [OLEDSHOT] / [TDSHOT] screenshots to PNG
and writes per node, into tools/bench/runs/harness/:

    clock-<node>-<stamp>.jsonl      one JSON object per parsed event (host time "t" in unix seconds)
    clock-<node>-<stamp>.summary    JSON: drift curve, jump sizes, longest display gap, skip histogram
    clock-<node>-<stamp>-oled-NNN.png / -tdeck-NNN.png

Offline: --replay NODE=FILE (repeatable) reads a recorded log instead of a port. A log line is
"YYYY-MM-DD HH:MM:SS.mmm  text" (serial_capture.py, local time), "<unix seconds>  text", or bare
text (inherits the previous stamp).

BENCH RULES. The tool only reads. The only thing it ever writes to a port are the instrument
console commands --clockstat, --timesrc all|cet|none, --oledshot, --tdshot (whitelist in
allowed_command()); nothing here transmits on LoRa. OPENING A PORT REBOOTS AN ESP32 (USB-UART
bridge reset): the run starts from the boot, wait for [BOOT];ready before judging anything. Ports
held by another process (lsof, Chrome Web Serial, serial_capture.py) are refused. Run
tools/bench/identity_guard.py first.

LINE CONTRACT (the INSTRUMENT firmware must print exactly this; fields are key;value pairs, ';' or
whitespace separated, so printfdeb's space form parses too):

  [CLOCK];stat;epoch;<s>;ms;<millis>;ticks;<n>;late_sum_ms;<n>;late_max_ms;<n>;lost_ms;<n>;src;<name>
  [CLOCK];set;src;<NTP|GPS|RTC|CET|PHONE|CMD>;old;<s>;new;<s>;delta_ms;<signed ms>
  [DISPCLK];draw;hms;<hh:mm:ss>;type;<iDisplayType>
  [DISPCLK];skip;<reason>        reason: type0|hold|setdisplay|off|ping|gate15
                                 (also accepted: [DISPCLK];skip;reason;<reason>)
  [OLEDSHOT];<w>x<h>;<base64>    u8g2 buffer, layout of GET /screen.oled (oled_screenshot.unpack)

  epoch / old / new are unix seconds (tsClock_m), integer or decimal.

  TDSHOT framing (not fixed by the campaign doc, defined here; the firmware wave must follow it):
  [TDSHOT];begin;<w>x<h>;<nbytes>;<nchunks>      e.g. begin;320x240;153600;<n>
  [TDSHOT];chunk;<idx>;<base64>                  idx 0..nchunks-1, raw RGB565 big endian bytes
                                                 (same byte order as GET /screen.rgb565), one line each
  [TDSHOT];end
  Chunks may arrive only between begin and end; a missing chunk, a wrong total length or a new
  begin before end drops the frame and is counted in the summary ("shot_errors").
"""

from __future__ import annotations

import argparse
import base64
import binascii
import json
import re
import struct
import sys
import threading
import time
import zlib
from collections import Counter
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Any, Callable, Dict, Iterable, List, Optional, Tuple

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import bench_suite  # noqa: E402  (discover_ports, load_fleet, port_busy, list_comports)
import oled_screenshot  # noqa: E402  (unpack)
import tdeck_screenshot  # noqa: E402  (rgb565be_to_rgb888, WIDTH, HEIGHT)

OUT_DIR = HERE / "runs" / "harness"
OLED_SCALE = 4
SKIP_REASONS = ("type0", "hold", "setdisplay", "off", "ping", "gate15")
TIMESRC_VALUES = ("all", "cet", "none")
STAMP_ISO_RE = re.compile(r"^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}(?:\.\d+)?)\s{1,2}(.*)$")
STAMP_UNIX_RE = re.compile(r"^(\d{9,11}(?:\.\d+)?)\s{1,2}(.*)$")
EVENT_RE = re.compile(r"\[(CLOCK|DISPCLK|OLEDSHOT|TDSHOT)\][;\s]+(.*)$")
HMS_RE = re.compile(r"^\d{2}:\d{2}:\d{2}$")
DIM_RE = re.compile(r"^(\d+)x(\d+)$")
SEP_RE = re.compile(r"[;\s]+")


# ---------------------------------------------------------------- command whitelist

def allowed_command(cmd: str) -> str:
    """Return cmd if it is one of the permitted read-only instrument commands, else raise."""
    parts = cmd.split()
    if parts in (["--clockstat"], ["--oledshot"], ["--tdshot"]):
        return cmd
    if len(parts) == 2 and parts[0] == "--timesrc" and parts[1] in TIMESRC_VALUES:
        return cmd
    raise ValueError(f"command not allowed: {cmd!r}")


# ---------------------------------------------------------------- parsing

def parse_stamp(line: str, prev: float) -> Tuple[float, str]:
    """Split a replay log line into (host unix time, text)."""
    m = STAMP_ISO_RE.match(line)
    if m:
        fmt = "%Y-%m-%d %H:%M:%S.%f" if "." in m.group(1) else "%Y-%m-%d %H:%M:%S"
        try:
            return datetime.strptime(m.group(1), fmt).timestamp(), m.group(2)
        except ValueError:
            pass
    m = STAMP_UNIX_RE.match(line)
    if m:
        return float(m.group(1)), m.group(2)
    return prev, line


def _num(s: str) -> Optional[float]:
    try:
        return float(s)
    except ValueError:
        return None


def _pairs(tokens: List[str]) -> Dict[str, str]:
    return {tokens[i]: tokens[i + 1] for i in range(0, len(tokens) - 1, 2)}


def parse_event(text: str) -> Optional[Tuple[str, str, Dict[str, Any]]]:
    """Parse one text line into (tag, kind, fields), or None if it is not a well-formed event.

    Tags: CLOCK (stat|set), DISPCLK (draw|skip), OLEDSHOT (shot), TDSHOT (begin|chunk|end).
    """
    m = EVENT_RE.search(text)
    if not m:
        return None
    tag, rest = m.group(1), m.group(2).strip()
    tok = [t for t in SEP_RE.split(rest) if t != ""]
    if not tok:
        return None
    if tag == "OLEDSHOT":
        dm = DIM_RE.match(tok[0])
        if not dm or len(tok) != 2:
            return None
        return tag, "shot", {"w": int(dm.group(1)), "h": int(dm.group(2)), "b64": tok[1]}
    kind, args = tok[0], tok[1:]
    if tag == "CLOCK" and kind == "stat":
        kv = _pairs(args)
        ep = _num(kv.get("epoch", ""))
        if ep is None:
            return None
        f: Dict[str, Any] = {"epoch": ep, "src": kv.get("src", "")}
        for k in ("ms", "ticks", "late_sum_ms", "late_max_ms", "lost_ms"):
            v = _num(kv.get(k, ""))
            if v is not None:
                f[k] = int(v)
        return tag, kind, f
    if tag == "CLOCK" and kind == "set":
        kv = _pairs(args)
        old, new = _num(kv.get("old", "")), _num(kv.get("new", ""))
        src = kv.get("src", "")
        if not src or old is None or new is None:
            return None
        d = _num(kv.get("delta_ms", ""))
        if d is None:
            d = (new - old) * 1000.0
        return tag, kind, {"src": src, "old": old, "new": new, "delta_ms": int(round(d))}
    if tag == "DISPCLK" and kind == "draw":
        kv = _pairs(args)
        if not HMS_RE.match(kv.get("hms", "")):
            return None
        t = _num(kv.get("type", ""))
        return tag, kind, {"hms": kv["hms"], "type": None if t is None else int(t)}
    if tag == "DISPCLK" and kind == "skip":
        reason = args[1] if len(args) >= 2 and args[0] == "reason" else (args[0] if args else "")
        if not reason:
            return None
        return tag, kind, {"reason": reason}
    if tag == "TDSHOT" and kind == "begin":
        dm = DIM_RE.match(args[0]) if len(args) == 3 else None
        n, c = (_num(args[1]), _num(args[2])) if dm else (None, None)
        if not dm or n is None or c is None:
            return None
        return tag, kind, {"w": int(dm.group(1)), "h": int(dm.group(2)), "nbytes": int(n), "nchunks": int(c)}
    if tag == "TDSHOT" and kind == "chunk":
        idx = _num(args[0]) if len(args) == 2 else None
        if idx is None:
            return None
        return tag, kind, {"idx": int(idx), "b64": args[1]}
    if tag == "TDSHOT" and kind == "end" and len(args) == 0:
        return tag, kind, {}
    return None


# ---------------------------------------------------------------- shots

def _png(width: int, height: int, color_type: int, raw: bytes, bpp: int) -> bytes:
    """Minimal 8-bit PNG (color_type 0 gray, 2 RGB), no external tools."""
    stride = width * bpp
    body = b"".join(b"\x00" + raw[y * stride:(y + 1) * stride] for y in range(height))

    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, color_type, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(body, 6)) + chunk(b"IEND", b""))


def decode_oled(w: int, h: int, b64: str) -> bytes:
    """base64 u8g2 buffer -> PNG bytes (oled_screenshot.unpack, scaled OLED_SCALE x)."""
    buf = base64.b64decode(b64, validate=True)
    gray = oled_screenshot.unpack(buf, w, h)  # ValueError on size mismatch
    s = OLED_SCALE
    rows = []
    for y in range(h):
        row = bytes(v for x in range(w) for v in (gray[y * w + x],) * s)
        rows.extend([row] * s)
    return _png(w * s, h * s, 0, b"".join(rows), 1)


def decode_tdeck(w: int, h: int, raw565: bytes) -> bytes:
    """RGB565 big endian -> PNG bytes (tdeck_screenshot.rgb565be_to_rgb888)."""
    if len(raw565) != w * h * 2:
        raise ValueError(f"{len(raw565)} bytes do not fit {w}x{h} RGB565")
    return _png(w, h, 2, tdeck_screenshot.rgb565be_to_rgb888(raw565), 3)


class TdShotAssembler:
    """State machine for the [TDSHOT] begin / chunk / end framing."""

    def __init__(self) -> None:
        self.hdr: Optional[Dict[str, Any]] = None
        self.chunks: Dict[int, bytes] = {}
        self.errors = 0

    def feed(self, kind: str, f: Dict[str, Any]) -> Optional[Tuple[int, int, bytes]]:
        """Returns (w, h, rgb565) when a frame completes."""
        if kind == "begin":
            if self.hdr is not None:
                self.errors += 1
            self.hdr, self.chunks = f, {}
            return None
        if self.hdr is None:
            self.errors += 1
            return None
        if kind == "chunk":
            try:
                self.chunks[f["idx"]] = base64.b64decode(f["b64"], validate=True)
            except (binascii.Error, ValueError):
                self.errors += 1
            return None
        hdr, chunks, self.hdr, self.chunks = self.hdr, self.chunks, None, {}
        data = b"".join(chunks.get(i, b"") for i in range(hdr["nchunks"]))
        if len(chunks) != hdr["nchunks"] or len(data) != hdr["nbytes"] or hdr["nbytes"] != hdr["w"] * hdr["h"] * 2:
            self.errors += 1
            return None
        return hdr["w"], hdr["h"], data


# ---------------------------------------------------------------- per-node analysis

@dataclass
class NodeStats:
    node: str
    stats: List[Tuple[float, Dict[str, Any]]] = field(default_factory=list)   # (host t, fields)
    sets: List[Tuple[float, Dict[str, Any]]] = field(default_factory=list)
    draws: List[Tuple[float, str, Optional[int]]] = field(default_factory=list)
    skips: Counter = field(default_factory=Counter)
    malformed: int = 0
    shot_errors: int = 0
    shots: List[str] = field(default_factory=list)

    def add(self, t: float, tag: str, kind: str, f: Dict[str, Any]) -> None:
        if tag == "CLOCK" and kind == "stat":
            self.stats.append((t, f))
        elif tag == "CLOCK" and kind == "set":
            self.sets.append((t, f))
        elif tag == "DISPCLK" and kind == "draw":
            self.draws.append((t, f["hms"], f["type"]))
        elif tag == "DISPCLK" and kind == "skip":
            self.skips[f["reason"]] += 1

    def summary(self, gap_threshold_s: float = 1.5) -> Dict[str, Any]:
        out: Dict[str, Any] = {"node": self.node, "malformed_lines": self.malformed,
                               "shot_errors": self.shot_errors, "shots": self.shots}
        # Drift curve: node epoch advance minus host time advance, relative to the first stat.
        curve: List[Dict[str, float]] = []
        if self.stats:
            t0, e0 = self.stats[0][0], self.stats[0][1]["epoch"]
            for t, f in self.stats:
                curve.append({"t": round(t, 3), "elapsed_s": round(t - t0, 3),
                              "drift_s": round((f["epoch"] - e0) - (t - t0), 3),
                              "offset_s": round(f["epoch"] - t, 3)})
        out["stat_count"] = len(self.stats)
        out["drift_curve"] = curve
        if len(curve) >= 2:
            span = curve[-1]["elapsed_s"]
            out["drift_total_s"] = curve[-1]["drift_s"]
            out["drift_max_abs_s"] = max(abs(c["drift_s"]) for c in curve)
            out["drift_ppm"] = round(curve[-1]["drift_s"] / span * 1e6, 1) if span > 0 else None
        last = self.stats[-1][1] if self.stats else {}
        out["lost_ms_last"] = last.get("lost_ms")
        out["late_max_ms_max"] = max((f.get("late_max_ms", 0) for _, f in self.stats), default=None)
        # Jumps from [CLOCK];set.
        sets = [{"t": round(t, 3), "src": f["src"], "delta_ms": f["delta_ms"]} for t, f in self.sets]
        out["sets"] = sets
        by_src: Dict[str, Dict[str, int]] = {}
        for s in sets:
            d = by_src.setdefault(s["src"], {"count": 0, "max_abs_delta_ms": 0})
            d["count"] += 1
            d["max_abs_delta_ms"] = max(d["max_abs_delta_ms"], abs(s["delta_ms"]))
        out["set_by_src"] = by_src
        out["max_abs_set_delta_ms"] = max((abs(s["delta_ms"]) for s in sets), default=None)
        # Display clock.
        out["draw_count"] = len(self.draws)
        gaps = [(b[0] - a[0], a[0], a[1], b[1]) for a, b in zip(self.draws, self.draws[1:])]
        if gaps:
            g = max(gaps)
            out["display_gap_max_s"] = round(g[0], 3)
            out["display_gap_at"] = {"t": round(g[1], 3), "from_hms": g[2], "to_hms": g[3]}
            out["display_gaps_over_threshold"] = sum(1 for x in gaps if x[0] > gap_threshold_s)
        else:
            out["display_gap_max_s"] = None
        out["display_gap_threshold_s"] = gap_threshold_s
        out["skip_reasons"] = dict(sorted(self.skips.items()))
        return out


class NodeSink:
    """Collects events of one node and writes the .jsonl / PNG evidence."""

    def __init__(self, node: str, prefix: Path, jsonl: bool = True) -> None:
        self.stats = NodeStats(node)
        self.prefix = prefix
        self.td = TdShotAssembler()
        self._n_oled = 0
        self._n_td = 0
        self._f = open(f"{prefix}.jsonl", "w", encoding="utf-8") if jsonl else None
        self._lock = threading.Lock()

    def line(self, t: float, text: str) -> None:
        """Feed one stamped serial line."""
        with self._lock:
            if "[CLOCK]" not in text and "[DISPCLK]" not in text and "[OLEDSHOT]" not in text \
                    and "[TDSHOT]" not in text:
                return
            ev = parse_event(text)
            if ev is None:
                self.stats.malformed += 1
                return
            tag, kind, f = ev
            if tag == "OLEDSHOT":
                self._oled(t, f)
            elif tag == "TDSHOT":
                self._tdshot(t, kind, f)
            else:
                self.stats.add(t, tag, kind, f)
                self._emit({"t": round(t, 3), "node": self.stats.node, "tag": tag, "kind": kind, **f})

    def _emit(self, rec: Dict[str, Any]) -> None:
        if self._f:
            self._f.write(json.dumps(rec) + "\n")
            self._f.flush()

    def _save(self, t: float, kind: str, n: int, png: bytes) -> None:
        path = Path(f"{self.prefix}-{kind}-{n:03d}.png")
        path.write_bytes(png)
        self.stats.shots.append(path.name)
        self._emit({"t": round(t, 3), "node": self.stats.node, "tag": "SHOT", "kind": kind, "file": path.name})

    def _oled(self, t: float, f: Dict[str, Any]) -> None:
        try:
            png = decode_oled(f["w"], f["h"], f["b64"])
        except (ValueError, binascii.Error):
            self.stats.shot_errors += 1
            return
        self._n_oled += 1
        self._save(t, "oled", self._n_oled, png)

    def _tdshot(self, t: float, kind: str, f: Dict[str, Any]) -> None:
        err0 = self.td.errors
        res = self.td.feed(kind, f)
        self.stats.shot_errors += self.td.errors - err0
        if res is not None:
            self._n_td += 1
            self._save(t, "tdeck", self._n_td, decode_tdeck(*res))

    def close(self, gap_threshold_s: float = 1.5) -> Dict[str, Any]:
        with self._lock:
            if self._f:
                self._f.close()
            summ = self.stats.summary(gap_threshold_s)
            Path(f"{self.prefix}.summary").write_text(json.dumps(summ, indent=2) + "\n", encoding="utf-8")
            return summ


# ---------------------------------------------------------------- replay and live

def replay(sink: NodeSink, lines: Iterable[str]) -> None:
    prev = 0.0
    for raw in lines:
        t, text = parse_stamp(raw.rstrip("\r\n"), prev)
        prev = t
        sink.line(t, text)


def send_command(ser: Any, cmd: str) -> None:
    """Write one whitelisted console command, char by char like identity_guard."""
    allowed_command(cmd)
    for ch in cmd.encode():
        ser.write(bytes([ch]))
        time.sleep(0.02)
    ser.write(b"\n")
    ser.flush()


def run_live(sink: NodeSink, ser: Any, duration_s: float, startup: List[str], periodic: List[Tuple[float, str]],
             stop: threading.Event, clock: Callable[[], float] = time.time) -> None:
    """Read `ser` for duration_s; send startup commands after 3 s, periodic ones every N s."""
    end = clock() + duration_s
    t_start = clock()
    pending = list(startup)
    next_due = {c: t_start + 3.0 + every for every, c in periodic}
    buf = b""
    while clock() < end and not stop.is_set():
        if pending and clock() - t_start >= 3.0:
            send_command(ser, pending.pop(0))
        for cmd, due in list(next_due.items()):
            if clock() >= due:
                send_command(ser, cmd)
                next_due[cmd] = due + dict((c, e) for e, c in periodic)[cmd]
        # Read what has arrived (or block for one byte): a chunked read(4096) waits for the
        # 0.5 s timeout on a slow console and stamps every line in the chunk late.
        chunk = ser.read(getattr(ser, "in_waiting", 0) or 1)
        if not chunk:
            continue
        buf += chunk
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            sink.line(clock(), line.decode("utf-8", errors="replace").rstrip("\r"))


def open_port(dev: str) -> Any:
    import serial  # pyserial
    import identity_guard  # serial_dtr_for

    s = serial.Serial()
    s.port, s.baudrate, s.timeout = dev, 115200, 0.5
    s.dtr, s.rts = identity_guard.serial_dtr_for(dev), False  # same DTR rule as identity_guard
    s.open()
    return s


# ---------------------------------------------------------------- CLI

def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--node", action="append", default=[], metavar="FLEET_NAME",
                    help="fleet.json node (heltec-1, t-beam-92, t-deck-14); repeatable, at most 3")
    ap.add_argument("--replay", action="append", default=[], metavar="NODE=FILE",
                    help="analyse a recorded log instead of a port; repeatable")
    ap.add_argument("--duration", type=float, default=600.0, help="seconds to record (default 600)")
    ap.add_argument("--out", type=Path, default=OUT_DIR, help="output directory (default runs/harness)")
    ap.add_argument("--timesrc", choices=TIMESRC_VALUES, help="send '--timesrc X' after the start")
    ap.add_argument("--clockstat-every", type=float, metavar="S", help="send --clockstat every S seconds")
    ap.add_argument("--shot-every", type=float, metavar="S",
                    help="send --oledshot (--tdshot on t-deck-*) every S seconds")
    ap.add_argument("--gap-threshold", type=float, default=1.5,
                    help="display gap (s) counted as a miss; the 1 s cadence plus host jitter (default 1.5)")
    ap.add_argument("--yes-reboot", action="store_true",
                    help="required for live ports: opening a port reboots ESP32 nodes (USB-UART bridge reset)")
    return ap


def main(argv: Optional[List[str]] = None) -> int:
    args = build_parser().parse_args(argv)
    args.out.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    sinks: Dict[str, NodeSink] = {}

    def prefix(node: str) -> Path:
        return args.out / f"clock-{node}-{stamp}"

    for spec in args.replay:
        node, _, path = spec.partition("=")
        if not node or not path:
            print(f"bad --replay {spec!r}, want NODE=FILE", file=sys.stderr)
            return 2
        sink = NodeSink(node, prefix(node))
        with open(path, encoding="utf-8", errors="replace") as fh:
            replay(sink, fh)
        sinks[node] = sink
    if args.node:
        if len(args.node) > 3:
            print("at most three nodes", file=sys.stderr)
            return 2
        if not args.yes_reboot:
            print("live run reboots ESP32 nodes on port open; pass --yes-reboot", file=sys.stderr)
            return 2
        fleet = bench_suite.load_fleet()
        comports = bench_suite.list_comports()
        if comports is None:
            print("pyserial not installed", file=sys.stderr)
            return 2
        found = bench_suite.discover_ports(fleet, comports)
        devs: Dict[str, str] = {}
        for n in args.node:
            if n not in found:
                print(f"{n}: no attached port with its usb_serial", file=sys.stderr)
                return 2
            if bench_suite.port_busy(found[n]):
                print(f"{n}: {found[n]} is held by another process (lsof)", file=sys.stderr)
                return 2
            devs[n] = found[n]
        startup = [f"--timesrc {args.timesrc}"] if args.timesrc else []
        stop = threading.Event()
        threads = []
        for n, dev in devs.items():
            sinks[n] = NodeSink(n, prefix(n))
            periodic: List[Tuple[float, str]] = []
            if args.clockstat_every:
                periodic.append((args.clockstat_every, "--clockstat"))
            if args.shot_every:
                periodic.append((args.shot_every, "--tdshot" if n.startswith("t-deck") else "--oledshot"))
            th = threading.Thread(target=run_live, daemon=True,
                                  args=(sinks[n], open_port(dev), args.duration, startup, periodic, stop))
            th.start()
            threads.append(th)
        try:
            for th in threads:
                th.join()
        except KeyboardInterrupt:
            stop.set()
            for th in threads:
                th.join(5.0)
    if not sinks:
        print("nothing to do: give --node or --replay", file=sys.stderr)
        return 2
    for node, sink in sinks.items():
        summ = sink.close(args.gap_threshold)
        print(f"{node}: stats={summ['stat_count']} draws={summ['draw_count']} sets={len(summ['sets'])} "
              f"drift_total_s={summ.get('drift_total_s')} display_gap_max_s={summ['display_gap_max_s']} "
              f"-> {sink.prefix}.summary")
    return 0


if __name__ == "__main__":
    sys.exit(main())
