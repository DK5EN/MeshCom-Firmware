#!/usr/bin/env python3
"""ETH-03 bench regression: inbound EXTUDP must not reset the nRF52.

Sends N JSON datagrams to the node's EXTUDP port (1799) while recording the
node's serial console, then decides PASS/FAIL from three signals:
  - the web server answers 200 before and after the burst,
  - no `[BOOT] RESETREAS=` line appears during the run (a reboot),
  - every `[EXT] Inc:` is followed by its `[EXT];rx;...;stack_hwm;N` line, and
    the smallest stack_hwm stays above --min-hwm bytes.

The node needs `--extudp on` and `--extudpip <this host>`; mesh and gateway
may be off. The destination is the operator's own SSID (DK5EN-1), never a
group broadcast (memory: no-broadcast-test-messages).

Fails on d05a0dc3 (one datagram resets DK5EN-90, RESETREAS=0x4) and passes
with the 8 kB loop task (src/main.cpp, ETH-03).

usage: eth03_probe.py --port /dev/cu.usbmodem2101 --ip 192.168.68.66 [--n 4] [--gap 0.5]
"""
import argparse, socket, sys, time, urllib.request, re
import serial


def http_code(ip, timeout=3.0):
    try:
        with urllib.request.urlopen(f"http://{ip}/", timeout=timeout) as r:
            return r.status
    except Exception:
        return 0


class Capture:
    """Reconnecting reader: the RAK de-enumerates on reset, so a lost port
    is itself evidence and must not end the capture."""

    def __init__(self, port):
        self.port, self.s, self.lines = port, None, []

    def _open(self):
        try:
            self.s = serial.Serial(self.port, 115200, timeout=0.05)
            self.s.dtr = True   # native USB: mute without DTR
            self.lines.append(("--- port open", time.time()))
        except (OSError, serial.SerialException):
            self.s = None

    def pump(self, seconds):
        end = time.time() + seconds
        while time.time() < end:
            if self.s is None:
                self._open()
                if self.s is None:
                    time.sleep(0.2)
                    continue
            try:
                line = self.s.readline()
            except (OSError, serial.SerialException) as e:
                self.lines.append((f"--- port lost: {e}", time.time()))
                self.s = None
                continue
            if line:
                self.lines.append((line.decode(errors="replace").rstrip("\r\n"), time.time()))

    def send(self, cmd):
        if self.s:
            self.s.write((cmd + "\r").encode())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--ip", required=True)
    ap.add_argument("--n", type=int, default=4)
    ap.add_argument("--gap", type=float, default=0.5)
    ap.add_argument("--min-hwm", type=int, default=128,
                    help="required loop-stack headroom in the units the node prints: "
                         "WORDS on nRF52 (x4 = bytes, so 128 = 512 B), bytes on ESP32")
    ap.add_argument("--settle", type=float, default=8.0)
    ap.add_argument("--log", default=None)
    a = ap.parse_args()

    cap = Capture(a.port)
    cap.pump(1.0)
    before = http_code(a.ip)
    print(f"http before: {before}")
    tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    payload = b'{"type":"msg","dst":"DK5EN-1","msg":"eth03 probe"}'
    for _ in range(a.n):
        tx.sendto(payload, (a.ip, 1799))
        cap.pump(a.gap)
    cap.pump(a.settle)
    after = http_code(a.ip)
    print(f"http after:  {after}")

    text = [l for l, _ in cap.lines]
    if a.log:
        with open(a.log, "w") as f:
            for l, t in cap.lines:
                f.write(time.strftime("%H:%M:%S", time.localtime(t)) + " " + l + "\n")

    reboots = [l for l in text if "RESETREAS=" in l]
    lost = [l for l in text if l.startswith("--- port lost")]
    incs = [l for l in text if "[EXT] Inc:" in l]
    hwms = [int(m.group(1)) for l in text for m in [re.search(r"\[EXT\];rx;len;\d+;stack_hwm;(\d+);", l)] if m]
    print(f"inc lines: {len(incs)}  rx lines: {len(hwms)}  min stack_hwm: {min(hwms) if hwms else 'n/a'}  reboots: {len(reboots)}  port lost: {len(lost)}")

    ok = (before == 200 and after == 200 and not reboots and not lost
          and a.n > 0 and len(hwms) == len(incs) == a.n and min(hwms) >= a.min_hwm)
    print("=== overall", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
