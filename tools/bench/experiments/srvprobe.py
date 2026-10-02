#!/usr/bin/env python3
"""TM-39: do the three country gateway servers (OE/DL/IT) answer UDP the same way?

Drives one bench node through `--gateway srv OE`, `DL`, `IT` in turn (each
change needs a reboot -- the destination host is chosen once at connect time,
in startMeshComUDP(), see udp_functions.cpp/nrf_eth.cpp), watches it for
--seconds per country, then restores the node to its resting state
(`--gateway off`, `--gateway srv OE`, `--udplog off`) and confirms with
`--info`.

Usage
  cd tools/bench/runs
  python3 ../experiments/srvprobe.py --port /dev/cu.usbserial-0001
  python3 ../experiments/srvprobe.py --countries OE,IT --seconds 60
  python3 ../experiments/srvprobe.py --parse-only srvprobe_20260830-*/

Markers consumed (all raw Serial.printf -- printfdeb needs --debug and
strips ';' outside csv, these do neither):
  [BOOT];ready;ms;N;ip;X                              esp32_main.cpp
  [GW];srv;<OE|DL|IT>;host;<host>;path;hamnet|inet;ms;N   TM-39, udp_functions.cpp/nrf_eth.cpp
  [GW];rx;type;SET|CET|BEAT|DATA|OTHER|CONF;len;N;ms;N    TM-39, both platforms' gateway RX path
  [GW];keep;tx;ok;O;ms;N                              TM-39, sendKEEP() (shared)
  [UDP];rx;ip;I;port;P;len;L;head;H                   TM-31 instrument (ESP32), gated by --udplog
  [UDP];tx;ip;I;port;P;len;L;ok;O                     TM-31 instrument (ESP32), gated by --udplog
  [UDP];log;0|1                                       echo of --udplog on/off
  [WIFI];dns;<name>;ip;<ip>;ms;N                       F6 async DNS
  [NTP];ok|timeout|txfail|kod;...                     TM-35 async NTP
  "...Server not responding for Ns..." / "...Heartbeat timeout Ns..."
                                                        esp32_main.cpp hb_warn -- printfdeb,
                                                        but neither format string uses ';' or
                                                        %f, so it is emitted unconditionally
  rst:0x..                                             ESP32 boot banner (reboot happened)

The reducer (SrvReducer) is pure -- it only ever sees a line iterator, never
a serial port -- so it is exercised without hardware by test_srvprobe.py and
reused by --parse-only against saved logs.
"""

import argparse
import glob
import os
import re
import sys
import time

try:
    import serial  # pyserial
except ImportError:  # pragma: no cover - exercised only when running for real
    serial = None

COUNTRIES = ("OE", "DL", "IT")

RE_READY = re.compile(r"\[BOOT\];ready;ms;(?P<ms>\d+);ip;(?P<ip>\d)")
RE_RESET = re.compile(r"rst:0x[0-9a-fA-F]+")
RE_GW_SRV = re.compile(
    r"\[GW\];srv;(?P<country>\w{2});host;(?P<host>[^;]+);path;(?P<path>hamnet|inet);ms;(?P<ms>\d+)"
)
RE_GW_RX = re.compile(r"\[GW\];rx;type;(?P<type>\w+);len;(?P<len>\d+);ms;(?P<ms>\d+)")
RE_GW_KEEP = re.compile(r"\[GW\];keep;tx;ok;(?P<ok>\d);ms;(?P<ms>\d+)")
RE_UDP_RX = re.compile(
    r"\[UDP\];rx;ip;(?P<ip>[\d.]+);port;(?P<port>\d+);len;(?P<len>\d+);head;(?P<head>[0-9A-Fa-f]+)"
)
RE_UDP_TX = re.compile(
    r"\[UDP\];tx;ip;(?P<ip>[\d.]+);port;(?P<port>\d+);len;(?P<len>\d+);ok;(?P<ok>\d)"
)
RE_UDP_LOG = re.compile(r"\[UDP\];log;(?P<on>\d)")
RE_WIFI_DNS = re.compile(r"\[WIFI\];dns;(?P<name>[^;]+);ip;(?P<ip>[\d.]+);ms;(?P<ms>\d+)")
RE_NTP = re.compile(r"\[NTP\];(?P<what>ok|timeout|txfail|kod)\b")
RE_INFO_GATEWAY = re.compile(r"Gateway\s+(on|off)")

# esp32_main.cpp hb_warn text. Not machine-formatted markers -- both format
# strings carry no ';' (printfdeb's non-csv stripping is a no-op here) and
# print unconditionally, --debug or not, so a plain substring match is
# reliable and does not need to track the em dash byte-for-byte.
HB_STAGE1 = "Server not responding for"
HB_STAGE2 = "Heartbeat timeout"


def pct(vals, q):
    if not vals:
        return None
    v = sorted(vals)
    k = min(len(v) - 1, int(round(q * (len(v) - 1))))
    return v[k]


class SrvReducer:
    """Reduces one country's line stream (a raw <XX>.log) to the facts the
    probe cares about. Never touches a serial port -- see test_srvprobe.py.
    """

    def __init__(self, country):
        self.country = country
        self.srv = None  # dict: country/host/path/ms, from [GW];srv
        self.keep_tx = []  # list of ms
        self.gw_rx_by_type = {}  # type -> count
        self.gw_rx_events = []  # (ms, type, len)
        self.udp_rx = []  # (ip, port, len) -- ESP32's [UDP];rx marker has no ms field
        self.udp_tx = []  # (ip, port, len, ok)
        self.dns = []  # (name, ip, ms)
        self.ntp = {}  # what -> count
        self.hb_warn_lines = []  # raw lines
        self.set_cet_lines = []  # opportunistic, see add()
        self.resets = 0
        self.ready = []  # (ms, ip)
        self.udplog_echo = []  # 0/1
        self.line_count = 0
        self._rx_seq = 0  # monotonic per-line counter, used as a latency clock
        # in place of a wall timestamp, since the RX/TX markers above don't
        # all carry their own device-uptime ms (the ESP32 [UDP];rx/tx TM-31
        # marker predates this probe and has no ms field).
        self._first_keep_seq = None
        self._first_answer_seq = None

    def add(self, line):
        self.line_count += 1
        seq = self.line_count

        if RE_RESET.search(line):
            self.resets += 1
            return

        m = RE_READY.search(line)
        if m:
            self.ready.append((int(m.group("ms")), int(m.group("ip"))))
            return

        m = RE_GW_SRV.search(line)
        if m:
            self.srv = {
                "country": m.group("country"),
                "host": m.group("host"),
                "path": m.group("path"),
                "ms": int(m.group("ms")),
            }
            return

        m = RE_GW_KEEP.search(line)
        if m:
            self.keep_tx.append(int(m.group("ms")))
            if self._first_keep_seq is None:
                self._first_keep_seq = seq
            return

        m = RE_GW_RX.search(line)
        if m:
            t = m.group("type")
            self.gw_rx_by_type[t] = self.gw_rx_by_type.get(t, 0) + 1
            ms = int(m.group("ms"))
            self.gw_rx_events.append((ms, t, int(m.group("len"))))
            if self._first_keep_seq is not None and self._first_answer_seq is None:
                self._first_answer_seq = seq
            return

        m = RE_UDP_RX.search(line)
        if m:
            self.udp_rx.append((m.group("ip"), int(m.group("port")), int(m.group("len"))))
            if self._first_keep_seq is not None and self._first_answer_seq is None:
                self._first_answer_seq = seq
            return

        m = RE_UDP_TX.search(line)
        if m:
            self.udp_tx.append(
                (m.group("ip"), int(m.group("port")), int(m.group("len")), int(m.group("ok")))
            )
            return

        m = RE_UDP_LOG.search(line)
        if m:
            self.udplog_echo.append(int(m.group("on")))
            return

        m = RE_WIFI_DNS.search(line)
        if m:
            self.dns.append((m.group("name"), m.group("ip"), int(m.group("ms"))))
            return

        m = RE_NTP.search(line)
        if m:
            w = m.group("what")
            self.ntp[w] = self.ntp.get(w, 0) + 1
            return

        if HB_STAGE1 in line or HB_STAGE2 in line:
            self.hb_warn_lines.append(line.strip())
            return

        if "{SET}" in line or "{CET}" in line:
            # Opportunistic only: neither [GW];rx nor [UDP];rx carries the
            # payload text, this only catches it if some other debug print
            # (e.g. printBuffer_aprs under --debug/--info) happened to be on.
            self.set_cet_lines.append(line.strip())

    # -- derived facts -----------------------------------------------------

    def first_answer_gap(self):
        """Number of markers between the first KEEP tx and the first inbound
        [GW];rx/[UDP];rx line after it -- a proxy for latency when neither
        side's marker carries a shared clock. None if no KEEP was seen, or no
        answer followed it in this window (silence)."""
        if self._first_keep_seq is None:
            return None
        if self._first_answer_seq is None:
            return None
        return self._first_answer_seq - self._first_keep_seq

    def total_rx(self):
        return sum(self.gw_rx_by_type.values())

    def silent(self):
        """True if we sent at least one KEEP and got nothing at all back
        (no [GW];rx of any type, no [UDP];rx) before the window closed."""
        return self._first_keep_seq is not None and self.total_rx() == 0 and not self.udp_rx

    def summary_row(self):
        host = self.srv["host"] if self.srv else "?"
        path = self.srv["path"] if self.srv else "?"
        dns_ip = self.dns[-1][1] if self.dns else ""
        gap = self.first_answer_gap()
        return {
            "country": self.country,
            "host": host,
            "path": path,
            "dns_ip": dns_ip,
            "keep_tx": len(self.keep_tx),
            "gw_rx_total": self.total_rx(),
            "gw_rx_by_type": dict(self.gw_rx_by_type),
            "udp_rx": len(self.udp_rx),
            "ntp": dict(self.ntp),
            "first_answer_gap": gap,
            "silent": self.silent(),
            "hb_warn": len(self.hb_warn_lines),
            "resets": self.resets,
        }


def render_table(rows):
    cols = [
        ("country", "country"),
        ("host", "host"),
        ("path", "path"),
        ("keep_tx", "keep"),
        ("gw_rx_total", "gw_rx"),
        ("udp_rx", "udp_rx"),
        ("ntp", "ntp"),
        ("first_answer_gap", "answer_gap"),
        ("hb_warn", "hb_warn"),
        ("resets", "resets"),
    ]
    lines = []
    widths = {}
    for key, head in cols:
        widths[key] = len(head)
    fmt_rows = []
    for r in rows:
        fr = dict(r)
        fr["ntp"] = ",".join(f"{k}:{v}" for k, v in sorted(r["ntp"].items())) or "-"
        fr["first_answer_gap"] = "-" if r["first_answer_gap"] is None else str(r["first_answer_gap"])
        fmt_rows.append(fr)
        for key, _ in cols:
            widths[key] = max(widths[key], len(str(fr[key])))
    head_line = "  ".join(h.ljust(widths[k]) for k, h in cols)
    lines.append(head_line)
    lines.append("  ".join("-" * widths[k] for k, _ in cols))
    for fr in fmt_rows:
        lines.append("  ".join(str(fr[k]).ljust(widths[k]) for k, _ in cols))
    return "\n".join(lines)


def render_verdicts(rows):
    verdicts = []
    by_country = {r["country"]: r for r in rows}

    hosts = {r["country"]: r["host"] for r in rows if r["host"] != "?"}
    if "OE" in hosts and "DL" in hosts and hosts["OE"] == hosts["DL"]:
        verdicts.append(
            f"DL and OE resolve to the same host ({hosts['DL']}) -- confirms the "
            "internet-path asymmetry read from udp_functions.cpp: on this WLAN "
            "(non-HAMNET) a DL-configured node is not special-cased and lands on "
            "the Austrian server, same as OE."
        )
    if "IT" in hosts and "OE" in hosts and hosts["IT"] != hosts["OE"]:
        verdicts.append(f"IT resolves to its own host ({hosts['IT']}), distinct from OE/DL.")

    for c, r in sorted(by_country.items()):
        if r["silent"]:
            verdicts.append(
                f"{c}: sent {r['keep_tx']} KEEP with 0 replies of any kind in the window -- "
                "server silent from this node's point of view."
            )
        elif r["gw_rx_total"] == 0 and r["udp_rx"] > 0:
            verdicts.append(
                f"{c}: {r['udp_rx']} raw UDP datagram(s) arrived but none classified as a "
                "GATE/BEAT frame -- check for CONF or other unhandled indicators."
            )
        if r["hb_warn"] > 0:
            verdicts.append(
                f"{c}: {r['hb_warn']} heartbeat-timeout warning line(s) logged "
                f"(HB_WARN_TIME=35s / MAX_HB_RX_TIME=65s) -- the server missed at "
                "least one full 30 s KEEP/BEAT cycle."
            )

    counts = {c: by_country[c]["gw_rx_total"] for c in by_country}
    if len(set(counts.values())) > 1 and all(by_country[c]["keep_tx"] for c in by_country):
        verdicts.append(
            "Reply volume differs across countries with comparable KEEP counts: "
            + ", ".join(f"{c}={n}" for c, n in sorted(counts.items()))
        )

    if not verdicts:
        verdicts.append("No differences observed across the countries run in this session.")
    return verdicts


# --------------------------------------------------------------------------
# Hardware runner
# --------------------------------------------------------------------------

BOOT_TIMEOUT = 40.0
SRV_TIMEOUT = 15.0


def open_port(port, baud=115200):
    s = serial.Serial()
    s.port = port
    s.baudrate = baud
    s.timeout = 0.2
    s.dtr = False
    s.rts = False
    s.open()
    return s


def send(ser, cmd):
    # The firmware consumes one character per loop() pass (same pacing as
    # wifisoak.py's Board.send) -- writing the whole line at once has been
    # observed to drop characters under load.
    for ch in cmd:
        ser.write(ch.encode())
        ser.flush()
        time.sleep(0.02)
    ser.write(b"\n")
    ser.flush()


class LineSource:
    """Reads a serial port, tees every line to a logfile (with a wall-clock
    prefix) and lets the caller wait for a pattern or just drain for N s."""

    def __init__(self, ser, logf):
        self.ser = ser
        self.logf = logf
        self.buf = b""

    def _pump(self, timeout):
        end = time.time() + timeout
        lines = []
        while time.time() < end:
            chunk = self.ser.read(4096)
            if not chunk:
                continue
            self.buf += chunk
            while b"\n" in self.buf:
                raw, self.buf = self.buf.split(b"\n", 1)
                line = raw.decode("utf-8", errors="replace").rstrip("\r")
                wall = time.strftime("%Y-%m-%d %H:%M:%S")
                self.logf.write(f"{wall} {line}\n")
                self.logf.flush()
                lines.append(line)
        return lines

    def wait_for(self, pattern, timeout):
        end = time.time() + timeout
        while time.time() < end:
            for line in self._pump(min(1.0, max(0.05, end - time.time()))):
                if pattern.search(line):
                    return line
        return None

    def drain(self, seconds):
        self._pump(seconds)


def run_probe(port, countries, seconds, outdir):
    if serial is None:
        sys.exit("pyserial is required to run against hardware (or use --parse-only)")

    os.makedirs(outdir, exist_ok=True)
    ser = open_port(port)
    print(f"opened {port} (this resets the node) -- waiting for [BOOT];ready")

    boot_log = open(os.path.join(outdir, "boot.log"), "w", encoding="utf-8")
    src = LineSource(ser, boot_log)
    if src.wait_for(RE_READY, BOOT_TIMEOUT) is None:
        print("WARNING: no [BOOT];ready seen after opening the port -- continuing anyway")

    rows = []
    for xx in countries:
        logpath = os.path.join(outdir, f"{xx}.log")
        with open(logpath, "w", encoding="utf-8") as logf:
            src = LineSource(ser, logf)
            print(f"{xx}: --gateway srv {xx} / --gateway on / --reboot")
            send(ser, f"--gateway srv {xx}")
            time.sleep(0.3)
            send(ser, "--gateway on")
            time.sleep(0.3)
            send(ser, "--reboot")

            if src.wait_for(RE_READY, BOOT_TIMEOUT) is None:
                print(f"{xx}: WARNING no [BOOT];ready within {BOOT_TIMEOUT:.0f}s")
            if src.wait_for(RE_GW_SRV, SRV_TIMEOUT) is None:
                print(f"{xx}: WARNING no [GW];srv within {SRV_TIMEOUT:.0f}s of boot")

            print(f"{xx}: --udplog on, observing {seconds:.0f}s")
            send(ser, "--udplog on")
            src.drain(seconds)
            send(ser, "--udplog off")
            src.drain(1.0)

        red = SrvReducer(xx)
        with open(logpath, encoding="utf-8", errors="replace") as fh:
            for line in fh:
                red.add(line[20:] if len(line) > 20 else line)
        rows.append(red.summary_row())
        print(f"{xx}: {red.summary_row()}")

    print("restoring: --gateway srv OE / --gateway off / --udplog off / --reboot")
    send(ser, "--gateway srv OE")
    time.sleep(0.3)
    send(ser, "--gateway off")
    time.sleep(0.3)
    send(ser, "--udplog off")
    time.sleep(0.3)
    send(ser, "--reboot")

    restore_log = open(os.path.join(outdir, "restore.log"), "w", encoding="utf-8")
    src = LineSource(ser, restore_log)
    src.wait_for(RE_READY, BOOT_TIMEOUT)
    send(ser, "--info")
    info_line = src.wait_for(RE_INFO_GATEWAY, 10.0)
    restore_log.close()
    ser.close()

    ok = bool(info_line and "off" in info_line)
    print(f"restore confirmed by --info: {info_line!r} ({'OK' if ok else 'CHECK MANUALLY'})")

    write_summary(outdir, rows, restored_ok=ok)
    return rows


def write_summary(outdir, rows, restored_ok=None):
    parts = [f"srvprobe {outdir}", "", render_table(rows), "", "Verdicts:"]
    for v in render_verdicts(rows):
        parts.append(f"- {v}")
    if restored_ok is not None:
        parts.append("")
        parts.append(
            "Node restored to --gateway off / --gateway srv OE / --udplog off: "
            + ("confirmed by --info." if restored_ok else "NOT CONFIRMED -- check the node by hand.")
        )
    text = "\n".join(parts) + "\n"
    with open(os.path.join(outdir, "summary.txt"), "w", encoding="utf-8") as fh:
        fh.write(text)
    print()
    print(text)


# --------------------------------------------------------------------------
# --parse-only
# --------------------------------------------------------------------------


def parse_only(paths):
    rows = []
    for p in paths:
        name = os.path.splitext(os.path.basename(p))[0].upper()
        if name not in COUNTRIES:
            continue
        red = SrvReducer(name)
        with open(p, encoding="utf-8", errors="replace") as fh:
            for line in fh:
                red.add(line[20:] if len(line) > 20 else line)
        rows.append(red.summary_row())
    if not rows:
        sys.exit("no OE.log/DL.log/IT.log matched")
    rows.sort(key=lambda r: COUNTRIES.index(r["country"]) if r["country"] in COUNTRIES else 99)
    write_summary(os.path.dirname(paths[0]) or ".", rows)


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("--port", default="/dev/cu.usbserial-0001")
    ap.add_argument("--countries", default="OE,DL,IT")
    ap.add_argument("--seconds", type=float, default=180.0)
    ap.add_argument("--label", default="")
    ap.add_argument(
        "--parse-only",
        nargs="+",
        metavar="LOG_OR_DIR",
        help="re-reduce existing <XX>.log files (or a run directory) without touching hardware",
    )
    args = ap.parse_args()

    if args.parse_only is not None:
        paths = []
        for pat in args.parse_only:
            if os.path.isdir(pat):
                paths.extend(sorted(glob.glob(os.path.join(pat, "*.log"))))
            else:
                paths.extend(sorted(glob.glob(pat)))
        parse_only(paths)
        return

    countries = [c.strip().upper() for c in args.countries.split(",") if c.strip()]
    for c in countries:
        if c not in COUNTRIES:
            sys.exit(f"unknown country {c!r}, expected one of {COUNTRIES}")

    outdir = f"srvprobe_{args.label + '_' if args.label else ''}{time.strftime('%Y%m%d-%H%M%S')}"
    run_probe(args.port, countries, args.seconds, outdir)


if __name__ == "__main__":
    main()
