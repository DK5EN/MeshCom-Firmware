"""Net console (2323) logger for gwrun.py: conlog.py <host> <call> <logfile> <cmdfile>

Appends timestamped lines to <logfile>, reconnects on drop, and types every new line appended to
<cmdfile> into the console. Login and password (MC_PW_<CALL> in .env) via tools/mc_console.py; a
refused login ends the logger. Line format "HH:MM:SS line" is what gwanalyze.py parses, shared with
gwseriallog.py -- which is why this is not tools/meshlogger.py.
"""
import os, socket, sys, time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
import mc_console  # noqa: E402

host, call, logfile, cmdfile = sys.argv[1:5]


def log(line: str) -> None:
    with open(logfile, "a") as f:
        f.write("%s %s\n" % (time.strftime("%H:%M:%S"), line))


cmdpos = os.path.getsize(cmdfile) if os.path.exists(cmdfile) else 0
con = mc_console.Console(host, call=call)
while True:
    try:
        pending = con.connect()
        s = con.sock
        s.settimeout(0.5)
        log("## connected")
        while True:
            while b"\n" in pending:
                ln, pending = pending.split(b"\n", 1)
                log(ln.decode("utf-8", "replace").rstrip("\r"))
            try:
                d = s.recv(4096)
                if not d:
                    raise OSError("closed")
                pending += d
            except socket.timeout:
                pass
            if os.path.exists(cmdfile) and os.path.getsize(cmdfile) > cmdpos:
                with open(cmdfile) as f:
                    f.seek(cmdpos)
                    new = f.read()
                    cmdpos = f.tell()
                for c in new.splitlines():
                    if c.strip():
                        log("## >> " + c)
                        for ch in c + "\n":
                            s.send(ch.encode())
                            time.sleep(0.02)
                        time.sleep(1.0)
    except mc_console.LoginError as e:
        log("## login refused, giving up: %s" % e)
        sys.exit(2)
    except Exception as e:  # noqa: BLE001
        con.close()
        log("## dropped: %r" % (e,))
        time.sleep(3)
