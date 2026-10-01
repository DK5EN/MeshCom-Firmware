#!/usr/bin/env python3
"""Raw serial capture to disk with host timestamps, for a days-long bench run.

    python3 tools/bench/serial_capture.py /dev/cu.usbserial-0001 ~/meshlog/dk5en-1.log
    python3 tools/bench/serial_capture.py /dev/cu.usbmodem1101 ~/meshlog/dk5en-90.log \\
        --dtr --until "2026-10-01 16:00" --cmd="--nbrdebug on" --cmd="--loradebug on"

Opens the port with DTR/RTS low by default (a CP2102 Heltec still reboots once on
open). Native-USB boards (RAK4631, S3) stay mute until DTR is asserted -- pass
--dtr for them. Prefixes every line with "YYYY-MM-DD HH:MM:SS.mmm  " (the format
tools/nbrlog.py, tools/nbrsnap.py and tools/nbrrelay.py parse), appends to OUTFILE,
reopens the port after an error with 5 s backoff. Each --cmd is sent after every
(re)open. --until ends the run at that local time; otherwise stop with
SIGTERM/Ctrl-C. Stdlib + pyserial.
"""
import argparse
import sys
import time
from datetime import datetime

import serial  # pyserial


def stamp() -> str:
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port")
    ap.add_argument("out")
    ap.add_argument("--dtr", action="store_true", help="assert DTR (native-USB boards)")
    ap.add_argument("--until", help='end time, local, "YYYY-MM-DD HH:MM"')
    ap.add_argument("--cmd", action="append", default=[], help="command sent after every open")
    args = ap.parse_args()
    end = datetime.strptime(args.until, "%Y-%m-%d %H:%M") if args.until else None

    def log(msg: str) -> None:
        with open(args.out, "a", encoding="utf-8") as f:
            f.write(f"{stamp()}  [CAPTURE] {msg}\n")

    while end is None or datetime.now() < end:
        try:
            s = serial.Serial()
            s.port = args.port
            s.baudrate = 115200
            s.timeout = 1.0
            s.dtr = args.dtr
            s.rts = False
            s.open()
            log(f"open {args.port} dtr={int(args.dtr)}")
            if args.cmd:
                time.sleep(2.0)
                for c in args.cmd:
                    s.write((c + "\r\n").encode())
                    s.flush()
                    time.sleep(0.5)
                log(f"sent {args.cmd}")
            with open(args.out, "a", encoding="utf-8", errors="replace") as f:
                buf = b""
                while end is None or datetime.now() < end:
                    chunk = s.read(4096)
                    if not chunk:
                        continue
                    buf += chunk
                    while b"\n" in buf:
                        line, buf = buf.split(b"\n", 1)
                        text = line.decode("utf-8", errors="replace").rstrip("\r")
                        f.write(f"{stamp()}  {text}\n")
                    f.flush()
            s.close()
        except KeyboardInterrupt:
            log("stopped (interrupt)")
            return 0
        except Exception as e:  # noqa: BLE001 -- keep the capture alive across port glitches
            log(f"error {e!r}, retry in 5 s")
            time.sleep(5)
    log(f"end of window {args.until}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
