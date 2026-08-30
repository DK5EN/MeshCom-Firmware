#!/usr/bin/env python3
"""TM-34 WiFi boot-loop runner: N power cycles, one row of WiFi facts per boot.

Opening the USB port reboots every ESP32 bench node, so a boot loop needs no
relay: open, capture, close, repeat. Each boot is written to its own raw log and
reduced to one CSV row; the summary at the end is the arm's headline number.

Usage
  cd tools/bench/runs
  python3 ../experiments/bootloop.py --arm A0 --boots 24
  python3 ../experiments/bootloop.py --arm A2 --boots 24 --port /dev/cu.usbserial-0001
  python3 ../experiments/bootloop.py --parse-only bootloop_A0_*/boot_*.log

Markers consumed (all already emitted by the firmware except [WIFI];stall, F5):
  ESP-ROM:                                       reset detected
  [WIFI]...SSID: X CHAN: n RSSI: r BSSID: mac    one per AP seen in our scan
  [WIFI]...connecting to CHAN: n BSSID: mac      BSSID-pinned join (arm A0/A1)
  [WIFI]...try connecting to SSID: x             SSID-only join
  [WIFI];event;connected;ms;N
  [WIFI];event;got_ip;ms;N
  [WIFI];event;disconnected;reason;R;ms;N
  [WIFI];stall;<site>;ms;N;task;<name>
  [BOOT];ready;ms;N;ip;X
  [INSTR-LOOP];n;..;total_us;..;avg_us;..;max_us;N

printfdeb renders ';' as a space outside `--debug csv`, so every pattern below
accepts ';' or whitespace as the separator.
"""

import argparse
import csv
import glob
import os
import re
import statistics
import sys
import time

try:
    import serial  # pyserial
except ImportError:  # --parse-only does not need it
    serial = None

DEFAULT_PORT = "/dev/cu.usbmodem1101"  # DK5EN-14, T-Deck Plus
SEP = r"[;\s]"

# CLIENT SETUP: the Heltec V3 (CP2102) loses the ROM banner on the port open,
# the first line captured is the sketch's own setup banner -- still a fresh boot.
RE_RESET = re.compile(r"ESP-ROM:|rst:0x|CLIENT SETUP")
RE_SCAN_AP = re.compile(
    r"\[WIFI\]\.\.\.SSID:\s*(?P<ssid>\S+)\s+CHAN:\s*(?P<chan>\d+)\s+"
    r"RSSI:\s*(?P<rssi>-?\d+)\s+BSSID:\s*(?P<bssid>[0-9A-Fa-f:]{17})"
)
RE_JOIN_PINNED = re.compile(
    r"\[WIFI\]\.\.\.connecting to CHAN:\s*(?P<chan>\d+)\s+BSSID:\s*(?P<bssid>[0-9A-Fa-f:]{17})"
)
RE_JOIN_SSID = re.compile(r"\[WIFI\]\.\.\.try connecting to SSID:")
RE_WAIT = re.compile(r"\[WIFI\]\.\.\.Wait connect")
RE_CONNECTED = re.compile(r"\[WIFI\]" + SEP + r"event" + SEP + r"connected" + SEP + r"ms" + SEP + r"(\d+)")
RE_GOT_IP = re.compile(r"\[WIFI\]" + SEP + r"event" + SEP + r"got_ip" + SEP + r"ms" + SEP + r"(\d+)")
RE_DISC = re.compile(
    r"\[WIFI\]" + SEP + r"event" + SEP + r"disconnected" + SEP + r"reason" + SEP
    + r"(?P<reason>\d+)" + SEP + r"ms" + SEP + r"(?P<ms>\d+)"
)
RE_STALL = re.compile(
    r"\[WIFI\]" + SEP + r"stall" + SEP + r"(?P<site>\S+?)" + SEP + r"ms" + SEP + r"(?P<ms>\d+)"
)
RE_READY = re.compile(r"\[BOOT\]" + SEP + r"ready" + SEP + r"ms" + SEP + r"(?P<ms>\d+)" + SEP + r"ip" + SEP + r"(?P<ip>\S+)")
RE_LOOPMAX = re.compile(r"\[INSTR-LOOP\].*max_us" + SEP + r"(\d+)")
RE_CONN_ERR = re.compile(r"\[WIFI\]\.\.\.ssid<[^>]*> connection error")

# WIFI_REASON_* values that show up on this bench; anything else prints raw.
REASONS = {
    2: "AUTH_EXPIRE",
    3: "AUTH_LEAVE",
    4: "ASSOC_EXPIRE",
    8: "ASSOC_LEAVE",
    15: "4WAY_HANDSHAKE_TIMEOUT",
    200: "BEACON_TIMEOUT",
    201: "NO_AP_FOUND",
    202: "AUTH_FAIL",
    203: "ASSOC_FAIL",
    204: "HANDSHAKE_TIMEOUT",
}

CSV_FIELDS = [
    "boot", "log", "reset_seen", "aps_seen", "best_rssi", "worst_rssi",
    "join_mode", "join_bssid", "connected_ms", "got_ip_ms", "ready_ms", "ready_ip",
    "disconnects", "reasons", "conn_errors", "loop_max_us", "stalls", "stall_max_ms",
    "first_join_ok",
]


def parse_boot(text, deadline_ms):
    """Reduce one boot's raw log to a dict of CSV_FIELDS (minus boot/log)."""
    aps = []
    row = {
        "reset_seen": 0, "aps_seen": 0, "best_rssi": "", "worst_rssi": "",
        "join_mode": "", "join_bssid": "", "connected_ms": "", "got_ip_ms": "",
        "ready_ms": "", "ready_ip": "", "disconnects": 0, "reasons": "",
        "conn_errors": 0, "loop_max_us": "", "stalls": 0, "stall_max_ms": "",
        "first_join_ok": 0,
    }
    reasons = []
    stall_ms = []

    for line in text.splitlines():
        if RE_RESET.search(line):
            row["reset_seen"] = 1
        m = RE_SCAN_AP.search(line)
        if m:
            aps.append((int(m.group("rssi")), m.group("bssid")))
        m = RE_JOIN_PINNED.search(line)
        if m:
            row["join_mode"] = "bssid"
            row["join_bssid"] = m.group("bssid")
        elif RE_JOIN_SSID.search(line):
            row["join_mode"] = "ssid"
        m = RE_CONNECTED.search(line)
        if m and not row["connected_ms"]:
            row["connected_ms"] = int(m.group(1))
        m = RE_GOT_IP.search(line)
        if m and not row["got_ip_ms"]:
            row["got_ip_ms"] = int(m.group(1))
        m = RE_DISC.search(line)
        if m:
            row["disconnects"] += 1
            reasons.append(int(m.group("reason")))
        m = RE_STALL.search(line)
        if m:
            row["stalls"] += 1
            stall_ms.append(int(m.group("ms")))
        m = RE_READY.search(line)
        if m and not row["ready_ms"]:
            row["ready_ms"] = int(m.group("ms"))
            row["ready_ip"] = m.group("ip")
        m = RE_LOOPMAX.search(line)
        if m:
            row["loop_max_us"] = int(m.group(1))
        if RE_CONN_ERR.search(line):
            row["conn_errors"] += 1

    row["aps_seen"] = len(aps)
    if aps:
        row["best_rssi"] = max(r for r, _ in aps)
        row["worst_rssi"] = min(r for r, _ in aps)
    if reasons:
        row["reasons"] = "|".join(f"{r}:{REASONS.get(r, '?')}" for r in reasons)
    if stall_ms:
        row["stall_max_ms"] = max(stall_ms)

    # The headline metric: associated AND addressed within the deadline, on the
    # first pass -- i.e. without the firmware having given up and restarted the
    # radio (which is what a conn_error burst signals).
    got_ip = row["got_ip_ms"]
    row["first_join_ok"] = int(isinstance(got_ip, int) and got_ip <= deadline_ms
                               and row["conn_errors"] == 0)
    return row


def capture_one(port, baud, seconds, settle):
    """Open the port (which resets the node), capture `seconds`, close."""
    s = serial.Serial()
    s.port = port
    s.baudrate = baud
    s.timeout = 0.2
    # Measured 2026-08-29 on the T-Deck Plus (ESP32-S3 USB-JTAG): with DTR/RTS
    # asserted at open the node does NOT reset (24/24 "no reset marker"); with
    # both cleared -- as tdeck_harness.TDeckSession does -- it reboots with
    # rst:0x15 USB_UART_CHIP_RESET. Same for the CP2102/CH9102 boards.
    s.dtr = False
    s.rts = False
    s.open()
    buf = bytearray()
    end = time.time() + seconds
    while time.time() < end:
        chunk = s.read(4096)
        if chunk:
            buf += chunk
    s.close()
    time.sleep(settle)  # let the USB device re-enumerate before the next open
    return buf.decode("utf-8", errors="replace")


def summarise(rows, deadline_ms):
    n = len(rows)
    ok = sum(r["first_join_ok"] for r in rows)
    got = [r["got_ip_ms"] for r in rows if isinstance(r["got_ip_ms"], int)]
    ready = [r["ready_ms"] for r in rows if isinstance(r["ready_ms"], int)]
    loop = [r["loop_max_us"] for r in rows if isinstance(r["loop_max_us"], int)]
    no_reset = sum(1 for r in rows if not r["reset_seen"])

    all_reasons = {}
    for r in rows:
        for item in filter(None, r["reasons"].split("|")):
            all_reasons[item] = all_reasons.get(item, 0) + 1

    out = []
    out.append(f"boots                     {n}")
    out.append(f"first join <= {deadline_ms/1000:.0f} s        {ok}/{n}")
    if got:
        out.append(f"got_ip ms  median/max     {int(statistics.median(got))} / {max(got)}"
                   f"   (n={len(got)})")
    if ready:
        out.append(f"ready ms   median/max     {int(statistics.median(ready))} / {max(ready)}")
    if loop:
        out.append(f"loop max us  median/max   {int(statistics.median(loop))} / {max(loop)}")
    out.append(f"disconnect events         {sum(r['disconnects'] for r in rows)}")
    for k in sorted(all_reasons, key=lambda x: -all_reasons[x]):
        out.append(f"    {k:<28} {all_reasons[k]}")
    out.append(f"connection-error bursts   {sum(r['conn_errors'] for r in rows)}")
    stalls = sum(r["stalls"] for r in rows)
    out.append(f"[WIFI];stall lines        {stalls}")
    if no_reset:
        out.append(f"!! boots with no reset marker: {no_reset} -- port open did not "
                   f"reboot the node; results for those rows are not a boot")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default=DEFAULT_PORT)
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--boots", type=int, default=24)
    ap.add_argument("--seconds", type=float, default=75.0,
                    help="capture window per boot (default 75)")
    ap.add_argument("--settle", type=float, default=2.0,
                    help="pause after closing the port, for USB re-enumeration")
    ap.add_argument("--arm", default="A0", help="arm label, goes in the directory name")
    ap.add_argument("--deadline-ms", type=int, default=25000,
                    help="uptime by which got_ip must arrive to count as a first join")
    ap.add_argument("--parse-only", nargs="*", metavar="LOG",
                    help="re-parse existing boot_*.log files instead of running")
    args = ap.parse_args()

    if args.parse_only is not None:
        paths = []
        for pattern in args.parse_only:
            paths.extend(sorted(glob.glob(pattern)))
        if not paths:
            sys.exit("no logs matched")
        rows = []
        for i, p in enumerate(paths, 1):
            with open(p, encoding="utf-8", errors="replace") as fh:
                r = parse_boot(fh.read(), args.deadline_ms)
            r["boot"] = i
            r["log"] = os.path.basename(p)
            rows.append(r)
        print(summarise(rows, args.deadline_ms))
        return

    if serial is None:
        sys.exit("pyserial is required to run (install it, or use --parse-only)")

    outdir = f"bootloop_{args.arm}_{time.strftime('%Y%m%d-%H%M%S')}"
    os.makedirs(outdir, exist_ok=True)
    print(f"arm {args.arm}: {args.boots} boots x {args.seconds:.0f} s on {args.port}")
    print(f"-> {outdir}/  (Ctrl-C stops; the CSV is written for the boots done so far)")

    rows = []
    try:
        for i in range(1, args.boots + 1):
            t0 = time.time()
            text = capture_one(args.port, args.baud, args.seconds, args.settle)
            log = os.path.join(outdir, f"boot_{i:03d}.log")
            with open(log, "w", encoding="utf-8") as fh:
                fh.write(text)
            row = parse_boot(text, args.deadline_ms)
            row["boot"] = i
            row["log"] = os.path.basename(log)
            rows.append(row)
            print(f"  boot {i:2d}/{args.boots}  join={'ok ' if row['first_join_ok'] else 'FAIL'}"
                  f"  aps={row['aps_seen']}  got_ip={row['got_ip_ms'] or '-'}"
                  f"  ready={row['ready_ms'] or '-'}  disc={row['disconnects']}"
                  f"  ({time.time()-t0:.0f} s)")
    except KeyboardInterrupt:
        print("\ninterrupted")

    if not rows:
        return
    csv_path = os.path.join(outdir, "summary.csv")
    with open(csv_path, "w", newline="", encoding="utf-8") as fh:
        w = csv.DictWriter(fh, fieldnames=CSV_FIELDS)
        w.writeheader()
        w.writerows(rows)

    text = summarise(rows, args.deadline_ms)
    with open(os.path.join(outdir, "summary.txt"), "w", encoding="utf-8") as fh:
        fh.write(f"arm {args.arm}  port {args.port}\n\n{text}\n")
    print()
    print(text)
    print(f"\n{csv_path}")


if __name__ == "__main__":
    main()
