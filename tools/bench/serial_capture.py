#!/usr/bin/env python3
"""Raw serial capture to disk with host timestamps, for a days-long bench run.

    python3 tools/bench/serial_capture.py /dev/cu.usbserial-0001 ~/meshlog/dk5en-1.log

Opens the port with DTR/RTS low (a CP2102 Heltec still reboots once on open),
prefixes every line with "YYYY-MM-DD HH:MM:SS.mmm  " (the format tools/nbrlog.py,
tools/nbrsnap.py and tools/nbrrelay.py parse), appends to OUTFILE, reopens the
port after an error with 5 s backoff. Stop with SIGTERM/Ctrl-C. Stdlib + pyserial.
"""
import sys
import time
from datetime import datetime

import serial  # pyserial


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    port, out = sys.argv[1], sys.argv[2]
    while True:
        try:
            s = serial.Serial()
            s.port = port
            s.baudrate = 115200
            s.timeout = 1.0
            s.dtr = False
            s.rts = False
            s.open()
            with open(out, "a", encoding="utf-8", errors="replace") as f:
                f.write(f"{datetime.now().strftime('%Y-%m-%d %H:%M:%S.%f')[:-3]}  [CAPTURE] open {port}\n")
                f.flush()
                buf = b""
                while True:
                    chunk = s.read(4096)
                    if not chunk:
                        continue
                    buf += chunk
                    while b"\n" in buf:
                        line, buf = buf.split(b"\n", 1)
                        text = line.decode("utf-8", errors="replace").rstrip("\r")
                        f.write(f"{datetime.now().strftime('%Y-%m-%d %H:%M:%S.%f')[:-3]}  {text}\n")
                    f.flush()
        except KeyboardInterrupt:
            return 0
        except Exception as e:  # noqa: BLE001 -- keep the capture alive across port glitches
            with open(out, "a", encoding="utf-8") as f:
                f.write(f"{datetime.now().strftime('%Y-%m-%d %H:%M:%S.%f')[:-3]}  [CAPTURE] error {e!r}, retry in 5 s\n")
            time.sleep(5)


if __name__ == "__main__":
    sys.exit(main())
