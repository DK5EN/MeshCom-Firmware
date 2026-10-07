"""Serial logger for the RAK4631 with the conlog.py cmdfile mechanism: gwseriallog.py <port> <logfile> <cmdfile> [baud]

Opens the port with DTR asserted (an nRF52 does not reboot on open), appends timestamped lines to
<logfile>, types every new line of <cmdfile> into the port. Reconnects on error. Exits on SIGTERM and closes the port.
"""
import os
import signal
import sys
import time
from typing import Optional

import serial  # pyserial

port, logfile, cmdfile = sys.argv[1:4]
baud = int(sys.argv[4]) if len(sys.argv) > 4 else 115200
stop = False


def _term(*_: object) -> None:
    global stop
    stop = True


signal.signal(signal.SIGTERM, _term)
signal.signal(signal.SIGINT, _term)


def log(line: str) -> None:
    with open(logfile, "a") as f:
        f.write("%s %s\n" % (time.strftime("%H:%M:%S"), line))


cmdpos = os.path.getsize(cmdfile) if os.path.exists(cmdfile) else 0
ser: Optional[serial.Serial] = None
buf = b""
while not stop:
    try:
        if ser is None:
            ser = serial.Serial(port, baud, timeout=0.3, dsrdtr=False, rtscts=False)
            ser.dtr = True
            log("## connected")
        buf += ser.read(4096)
        while b"\n" in buf:
            ln, buf = buf.split(b"\n", 1)
            log(ln.decode("utf-8", "replace").rstrip("\r"))
        if os.path.exists(cmdfile) and os.path.getsize(cmdfile) > cmdpos:
            with open(cmdfile) as f:
                f.seek(cmdpos)
                new = f.read()
                cmdpos = f.tell()
            for c in new.splitlines():
                if c.strip():
                    log("## >> " + c)
                    ser.write((c + "\r\n").encode())
                    ser.flush()
                    time.sleep(1.0)
    except Exception as e:  # noqa: BLE001
        log("## dropped: %r" % (e,))
        try:
            if ser:
                ser.close()
        except Exception:  # noqa: BLE001
            pass
        ser = None
        time.sleep(3)
if ser is not None:
    ser.close()
