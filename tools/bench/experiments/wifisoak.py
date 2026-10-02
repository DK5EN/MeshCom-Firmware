#!/usr/bin/env python3
"""TM-34 WiFi soak: hold N boards open for hours, drop the link periodically,
reduce the [WIFI] markers to per-event rows and a reconnect-time distribution.

One held-open USB session per board (opening resets every ESP32 bench node,
so the first boot of each run is a real boot). Every --drop-every seconds the
runner sends --wifidrop (driver-side disconnect + re-select, no config change)
to each board that reports its link up. Everything the node prints goes to a
raw log; the [WIFI] lines are reduced live to events.csv and, at the end or on
Ctrl-C, to summary.txt.

Usage
  cd tools/bench/runs
  python3 ../experiments/wifisoak.py --hours 12 \
      --board tdeck=/dev/cu.usbmodem1101 \
      --board heltec=/dev/cu.usbserial-0001 \
      --board tbeam=/dev/cu.usbserial-573C0005841
  python3 ../experiments/wifisoak.py --parse-only wifisoak_20260830-*/tdeck.log

Markers consumed (all raw Serial.printf, so ';' survives --debug off):
  [WIFI];event;connected|got_ip;ms;N
  [WIFI];event;disconnected;reason;R;ms;N
  [WIFI];assoc;<what>;ssid;S;bssid;M;chan;C;rssi;R;auth;A;phy;P;pmf;X;wpa3;Y;live;L;reason;R;ms;N
  [WIFI];link;up;rssi;R;bssid;M;chan;C;age_s;A;got_ip_n;K;ip;I;ms;N
  [WIFI];link;down;...
  [WIFI];stall;<site>;ms;N;task;T
  [WIFI];drop;ms;N                 (echo of our --wifidrop)
  [WIFI];harvest;same_ip;IP;ms;N
  [WIFI];watchdog;reconnect|reset;down_s;N;ms;N
  [WIFI];dns;<name>;ip;IP;ms;N
  [BOOT];ready;ms;N;ip;X
  rst:0x..                         (a reset after the first one is a crash/reboot)
"""

import argparse
import csv
import glob
import os
import re
import signal
import statistics
import sys
import threading
import time

try:
    import serial  # pyserial
except ImportError:
    serial = None

RE_RESET = re.compile(r"rst:0x[0-9a-f]+")
RE_EVENT = re.compile(r"\[WIFI\];event;(?P<what>connected|got_ip);ms;(?P<ms>\d+)")
RE_DISC = re.compile(r"\[WIFI\];event;disconnected;reason;(?P<reason>\d+);ms;(?P<ms>\d+)")
RE_ASSOC = re.compile(
    r"\[WIFI\];assoc;(?P<what>\w+);ssid;(?P<ssid>[^;]*);bssid;(?P<bssid>[0-9A-F:]{17});"
    r"chan;(?P<chan>\d+);rssi;(?P<rssi>-?\d+);auth;(?P<auth>\d+);phy;(?P<phy>\w*);"
    r"pmf;(?P<pmf>\d);wpa3;(?P<wpa3>\d);live;(?P<live>\d);reason;(?P<reason>\d+);ms;(?P<ms>\d+)"
)
RE_LINK_UP = re.compile(
    r"\[WIFI\];link;up;rssi;(?P<rssi>-?\d+);bssid;(?P<bssid>[0-9A-F:]{17});chan;(?P<chan>\d+);"
    r"age_s;(?P<age>\d+);got_ip_n;(?P<n>\d+);ip;(?P<ip>\d);ms;(?P<ms>\d+)"
)
RE_LINK_DOWN = re.compile(r"\[WIFI\];link;down;.*?;ms;(?P<ms>\d+)")
RE_STALL = re.compile(r"\[WIFI\];stall;(?P<site>[^;]+);ms;(?P<ms>\d+);task;(?P<task>\S+)")
RE_DROP = re.compile(r"\[WIFI\];drop;ms;(?P<ms>\d+)")
RE_HARVEST = re.compile(r"\[WIFI\];harvest;same_ip;(?P<ip>[\d.]+);ms;(?P<ms>\d+)")
RE_WD = re.compile(r"\[WIFI\];watchdog;(?P<what>\w+);down_s;(?P<down>\d+);ms;(?P<ms>\d+)")
RE_DNS = re.compile(r"\[WIFI\];dns;(?P<name>[^;]+);ip;(?P<ip>[\d.]+);ms;(?P<ms>\d+)")
RE_READY = re.compile(r"\[BOOT\][; ]ready[; ]ms[; ](?P<ms>\d+)[; ]ip[; ](?P<ip>\d)")

REASONS = {
    2: "AUTH_EXPIRE", 3: "AUTH_LEAVE", 4: "ASSOC_EXPIRE", 8: "ASSOC_LEAVE",
    15: "4WAY_HANDSHAKE_TIMEOUT", 200: "BEACON_TIMEOUT", 201: "NO_AP_FOUND",
    202: "AUTH_FAIL", 203: "ASSOC_FAIL", 204: "HANDSHAKE_TIMEOUT",
}

EVENT_FIELDS = ["board", "wall", "uptime_ms", "kind", "reason", "bssid", "chan", "rssi",
                "value", "note"]


def pct(vals, q):
    if not vals:
        return None
    v = sorted(vals)
    k = min(len(v) - 1, int(round(q * (len(v) - 1))))
    return v[k]


class Reducer:
    """Turns one board's line stream into events + statistics."""

    def __init__(self, board):
        self.board = board
        self.events = []
        self.resets = 0
        self.ready = []
        self.drops_sent = 0           # counted by the runner
        self.drop_pending_ms = None   # uptime of the last [WIFI];drop echo
        self.disc_pending_ms = None   # uptime of an unsolicited disconnect
        self.disc_pending_reason = None
        self.reconnect_after_drop = []
        self.reconnect_after_disc = []
        self.disc_reasons = {}
        self.assoc_bssid = {}         # bssid -> count of got_ip
        self.assoc_rows = []          # (uptime, bssid, chan, rssi, auth, pmf, wpa3)
        self.last_got_ip_bssid = None
        self.bssid_changes = 0
        self.stalls = []              # (site, ms, task)
        self.link_rssi = []
        self.link_up_last = None      # True/False
        self.harvest_same_ip = 0
        self.watchdog = []
        self.dns = []
        self.first_got_ip_ms = None
        self.last_uptime = 0

    def add(self, wall, line):
        b = self.board
        m = RE_RESET.search(line)
        if m:
            self.resets += 1
            self.events.append(dict(board=b, wall=wall, uptime_ms=0, kind="reset",
                                    reason="", bssid="", chan="", rssi="", value=m.group(0),
                                    note="" if self.resets == 1 else "UNEXPECTED"))
            self.drop_pending_ms = None
            self.disc_pending_ms = None
            return
        m = RE_READY.search(line)
        if m:
            self.ready.append((int(m.group("ms")), int(m.group("ip"))))
            self.events.append(dict(board=b, wall=wall, uptime_ms=int(m.group("ms")),
                                    kind="ready", reason="", bssid="", chan="", rssi="",
                                    value=m.group("ip"), note=""))
            return
        m = RE_DISC.search(line)
        if m:
            ms = int(m.group("ms"))
            r = int(m.group("reason"))
            self.last_uptime = ms
            name = f"{r}:{REASONS.get(r, '?')}"
            if self.drop_pending_ms is not None and r == 8:
                note = "our drop"
            else:
                note = "unsolicited"
                self.disc_reasons[name] = self.disc_reasons.get(name, 0) + 1
                if self.disc_pending_ms is None:
                    self.disc_pending_ms = ms
                    self.disc_pending_reason = name
            self.events.append(dict(board=b, wall=wall, uptime_ms=ms, kind="disconnected",
                                    reason=name, bssid="", chan="", rssi="", value="", note=note))
            return
        m = RE_ASSOC.search(line)
        if m:
            ms = int(m.group("ms"))
            self.last_uptime = ms
            what = m.group("what")
            row = dict(board=b, wall=wall, uptime_ms=ms, kind=f"assoc_{what}",
                       reason=m.group("reason"), bssid=m.group("bssid"), chan=m.group("chan"),
                       rssi=m.group("rssi"),
                       value=f"auth={m.group('auth')} phy={m.group('phy')} pmf={m.group('pmf')} wpa3={m.group('wpa3')}",
                       note="live" if m.group("live") == "1" else "cached")
            self.events.append(row)
            if what == "got_ip":
                bssid = m.group("bssid")
                self.assoc_bssid[bssid] = self.assoc_bssid.get(bssid, 0) + 1
                self.assoc_rows.append((ms, bssid, int(m.group("chan")), int(m.group("rssi")),
                                        int(m.group("auth")), int(m.group("pmf")), int(m.group("wpa3"))))
                if self.last_got_ip_bssid and bssid != self.last_got_ip_bssid:
                    self.bssid_changes += 1
                    row["note"] += " BSSID_CHANGE"
                self.last_got_ip_bssid = bssid
                if self.first_got_ip_ms is None:
                    self.first_got_ip_ms = ms
                if self.drop_pending_ms is not None:
                    self.reconnect_after_drop.append(ms - self.drop_pending_ms)
                    row["value"] += f" reconnect_ms={ms - self.drop_pending_ms}"
                    self.drop_pending_ms = None
                elif self.disc_pending_ms is not None:
                    self.reconnect_after_disc.append(ms - self.disc_pending_ms)
                    row["value"] += f" recover_ms={ms - self.disc_pending_ms} after {self.disc_pending_reason}"
                    self.disc_pending_ms = None
            return
        m = RE_EVENT.search(line)
        if m:
            self.last_uptime = int(m.group("ms"))
            return  # the assoc line carries the detail
        m = RE_LINK_UP.search(line)
        if m:
            self.last_uptime = int(m.group("ms"))
            self.link_rssi.append(int(m.group("rssi")))
            self.link_up_last = True
            self.events.append(dict(board=b, wall=wall, uptime_ms=int(m.group("ms")), kind="link",
                                    reason="up", bssid=m.group("bssid"), chan=m.group("chan"),
                                    rssi=m.group("rssi"), value=f"age_s={m.group('age')} ip={m.group('ip')}",
                                    note=""))
            return
        m = RE_LINK_DOWN.search(line)
        if m:
            self.last_uptime = int(m.group("ms"))
            self.link_up_last = False
            self.events.append(dict(board=b, wall=wall, uptime_ms=int(m.group("ms")), kind="link",
                                    reason="down", bssid="", chan="", rssi="", value="", note=""))
            return
        m = RE_STALL.search(line)
        if m:
            self.stalls.append((m.group("site"), int(m.group("ms")), m.group("task")))
            self.events.append(dict(board=b, wall=wall, uptime_ms=self.last_uptime, kind="stall",
                                    reason=m.group("site"), bssid="", chan="", rssi="",
                                    value=m.group("ms"), note=m.group("task")))
            return
        m = RE_DROP.search(line)
        if m:
            self.drop_pending_ms = int(m.group("ms"))
            self.disc_pending_ms = None
            self.events.append(dict(board=b, wall=wall, uptime_ms=int(m.group("ms")), kind="drop",
                                    reason="", bssid="", chan="", rssi="", value="", note=""))
            return
        m = RE_HARVEST.search(line)
        if m:
            self.harvest_same_ip += 1
            return
        m = RE_WD.search(line)
        if m:
            self.watchdog.append((m.group("what"), int(m.group("down"))))
            self.events.append(dict(board=b, wall=wall, uptime_ms=int(m.group("ms")), kind="watchdog",
                                    reason=m.group("what"), bssid="", chan="", rssi="",
                                    value=m.group("down"), note=""))
            return
        m = RE_DNS.search(line)
        if m:
            self.dns.append((m.group("name"), m.group("ip"), int(m.group("ms"))))
            return

    def summary(self):
        out = [f"== {self.board}"]
        out.append(f"resets seen               {self.resets}"
                   + ("" if self.resets <= 1 else f"   !! {self.resets - 1} unexpected reboot(s)"))
        if self.first_got_ip_ms is not None:
            out.append(f"first got_ip ms           {self.first_got_ip_ms}")
        if self.ready:
            out.append(f"ready ms / ip             {self.ready[0][0]} / {self.ready[0][1]}")
        out.append(f"drops sent                {self.drops_sent}")
        r = self.reconnect_after_drop
        if r:
            out.append(f"reconnect after drop ms   n={len(r)} median={int(statistics.median(r))} "
                       f"p90={pct(r, 0.9)} max={max(r)}")
        if self.drops_sent > len(r):
            out.append(f"!! drops without got_ip   {self.drops_sent - len(r)}")
        out.append(f"unsolicited disconnects   {sum(self.disc_reasons.values())}")
        for k in sorted(self.disc_reasons, key=lambda x: -self.disc_reasons[x]):
            out.append(f"    {k:<28} {self.disc_reasons[k]}")
        d = self.reconnect_after_disc
        if d:
            out.append(f"recover after disc ms     n={len(d)} median={int(statistics.median(d))} "
                       f"p90={pct(d, 0.9)} max={max(d)}")
        out.append(f"BSSID changes             {self.bssid_changes}")
        for bssid, n in sorted(self.assoc_bssid.items(), key=lambda kv: -kv[1]):
            rows = [x for x in self.assoc_rows if x[1] == bssid]
            rssis = [x[3] for x in rows]
            chans = sorted({x[2] for x in rows})
            auths = sorted({f"auth={x[4]} pmf={x[5]} wpa3={x[6]}" for x in rows})
            out.append(f"    {bssid} got_ip x{n:<4} chan {chans} rssi min/med/max "
                       f"{min(rssis)}/{int(statistics.median(rssis))}/{max(rssis)}  {', '.join(auths)}")
        out.append(f"harvest same_ip           {self.harvest_same_ip}")
        out.append(f"watchdog actions          {len(self.watchdog)}"
                   + (f"  {self.watchdog}" if self.watchdog else ""))
        if self.stalls:
            worst = max(self.stalls, key=lambda s: s[1])
            over = sum(1 for s in self.stalls if s[1] > 500)
            out.append(f"[WIFI];stall lines        {len(self.stalls)}  max {worst[1]} ms at {worst[0]} "
                       f"({worst[2]})  >500 ms: {over}")
        else:
            out.append("[WIFI];stall lines        0")
        if self.link_rssi:
            out.append(f"link rssi min/med/max     {min(self.link_rssi)}/"
                       f"{int(statistics.median(self.link_rssi))}/{max(self.link_rssi)}  (n={len(self.link_rssi)})")
        if self.dns:
            out.append(f"dns                       {len(self.dns)} resolutions, max {max(x[2] for x in self.dns)} ms")
        return "\n".join(out)


class Board(threading.Thread):
    def __init__(self, name, port, outdir, baud=115200):
        super().__init__(daemon=True)
        self.name = name
        self.port = port
        self.baud = baud
        self.red = Reducer(name)
        self.stop = threading.Event()
        self.lock = threading.Lock()
        self.log = open(os.path.join(outdir, f"{name}.log"), "a", buffering=1, encoding="utf-8")
        self.ev = open(os.path.join(outdir, f"{name}_events.csv"), "a", newline="", encoding="utf-8")
        self.evw = csv.DictWriter(self.ev, fieldnames=EVENT_FIELDS)
        self.evw.writeheader()
        self.ser = None
        self.n_events_written = 0
        self.error = None

    def run(self):
        try:
            s = serial.Serial()
            s.port = self.port
            s.baudrate = self.baud
            s.timeout = 0.2
            s.dtr = False
            s.rts = False
            s.open()
            self.ser = s
        except Exception as e:  # noqa: BLE001
            self.error = str(e)
            return
        buf = b""
        while not self.stop.is_set():
            try:
                chunk = self.ser.read(4096)
            except Exception as e:  # noqa: BLE001
                self.error = str(e)
                break
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                line = raw.decode("utf-8", errors="replace").rstrip("\r")
                wall = time.strftime("%Y-%m-%d %H:%M:%S")
                self.log.write(f"{wall} {line}\n")
                with self.lock:
                    self.red.add(wall, line)
                    self.flush_events()
        try:
            self.ser.close()
        except Exception:  # noqa: BLE001
            pass

    def flush_events(self):
        rows = self.red.events[self.n_events_written:]
        for r in rows:
            self.evw.writerow(r)
        self.n_events_written = len(self.red.events)

    def send(self, cmd):
        if self.ser is None:
            return False
        # the firmware consumes one char per loop(): pace the characters
        for ch in cmd:
            self.ser.write(ch.encode())
            self.ser.flush()
            time.sleep(0.02)
        self.ser.write(b"\n")
        self.ser.flush()
        return True


def parse_only(paths):
    for p in paths:
        name = os.path.basename(p).rsplit(".", 1)[0]
        red = Reducer(name)
        with open(p, encoding="utf-8", errors="replace") as fh:
            for line in fh:
                wall = line[:19]
                red.add(wall, line[20:] if len(line) > 20 else line)
        red.drops_sent = sum(1 for e in red.events if e["kind"] == "drop")
        print(red.summary())
        print()


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--board", action="append", default=[],
                    help="name=/dev/port, repeatable")
    ap.add_argument("--hours", type=float, default=12.0)
    ap.add_argument("--drop-every", type=float, default=600.0,
                    help="seconds between --wifidrop per board (0 = never)")
    ap.add_argument("--settle", type=float, default=90.0,
                    help="seconds after open before the first drop")
    ap.add_argument("--summary-every", type=float, default=600.0)
    ap.add_argument("--label", default="")
    ap.add_argument("--parse-only", nargs="*", metavar="LOG")
    args = ap.parse_args()

    if args.parse_only is not None:
        paths = []
        for pat in args.parse_only:
            paths.extend(sorted(glob.glob(pat)))
        if not paths:
            sys.exit("no logs matched")
        parse_only(paths)
        return

    if serial is None:
        sys.exit("pyserial is required to run (or use --parse-only)")
    if not args.board:
        sys.exit("at least one --board name=/dev/port")

    outdir = f"wifisoak_{args.label + '_' if args.label else ''}{time.strftime('%Y%m%d-%H%M%S')}"
    os.makedirs(outdir, exist_ok=True)
    boards = []
    for spec in args.board:
        name, port = spec.split("=", 1)
        boards.append(Board(name, port, outdir))
    for b in boards:
        b.start()
    print(f"soak: {len(boards)} board(s), {args.hours:.1f} h, drop every {args.drop_every:.0f} s -> {outdir}/")

    stop = threading.Event()
    signal.signal(signal.SIGINT, lambda *_: stop.set())
    signal.signal(signal.SIGTERM, lambda *_: stop.set())

    t_start = time.time()
    t_end = t_start + args.hours * 3600
    next_drop = t_start + args.settle
    next_summary = t_start + args.summary_every

    def write_summary(final=False):
        parts = [f"wifisoak {outdir}  elapsed {(time.time() - t_start) / 3600:.2f} h"
                 + ("  (final)" if final else "  (interim)")]
        for b in boards:
            with b.lock:
                parts.append(b.red.summary())
            if b.error:
                parts.append(f"!! {b.name}: serial error: {b.error}")
        text = "\n\n".join(parts)
        with open(os.path.join(outdir, "summary.txt"), "w", encoding="utf-8") as fh:
            fh.write(text + "\n")
        return text

    while not stop.is_set() and time.time() < t_end:
        now = time.time()
        if args.drop_every > 0 and now >= next_drop:
            for b in boards:
                with b.lock:
                    up = b.red.link_up_last
                if up is False:
                    print(f"{time.strftime('%H:%M:%S')} {b.name}: link down, drop skipped")
                    continue
                if b.send("--wifidrop"):
                    with b.lock:
                        b.red.drops_sent += 1
                    print(f"{time.strftime('%H:%M:%S')} {b.name}: --wifidrop #{b.red.drops_sent}")
            next_drop = now + args.drop_every
        if now >= next_summary:
            print(write_summary())
            next_summary = now + args.summary_every
        if all(b.error for b in boards):
            print("all boards lost their serial port")
            break
        time.sleep(1.0)

    for b in boards:
        b.stop.set()
    for b in boards:
        b.join(timeout=3)
    print()
    print(write_summary(final=True))
    print(f"\n{outdir}/summary.txt")


if __name__ == "__main__":
    main()
