"""Net console (2323) logger with HMAC auth: conlog.py <host> <pwfile> <logfile> <cmdfile>

Appends timestamped lines to <logfile>, reconnects on drop, reads the password from <pwfile> on every
connect (empty file = open console), and types every new line appended to <cmdfile> into the console.
"""
import hashlib, hmac, os, socket, sys, time

host, pwfile, logfile, cmdfile = sys.argv[1:5]


def log(line: str) -> None:
    with open(logfile, "a") as f:
        f.write("%s %s\n" % (time.strftime("%H:%M:%S"), line))


def connect() -> socket.socket:
    pw = open(pwfile).read().strip().encode() if os.path.exists(pwfile) else b""
    s = socket.create_connection((host, 2323), timeout=10)
    s.settimeout(5)
    first = b""
    while b"\n" not in first:
        d = s.recv(1)
        if not d:
            raise OSError("closed before banner")
        first += d
    line = first.decode("utf-8", "replace").strip()
    if line.upper().startswith("NONCE:"):
        nonce = bytes.fromhex(line.split()[1])
        s.sendall((hmac.new(pw, nonce, hashlib.sha256).hexdigest() + "\n").encode())
    else:
        log("## no nonce, first line: " + line)
    s.settimeout(0.5)
    return s


cmdpos = os.path.getsize(cmdfile) if os.path.exists(cmdfile) else 0
while True:
    try:
        s = connect()
        log("## connected")
        pending = b""
        while True:
            try:
                d = s.recv(4096)
                if not d:
                    raise OSError("closed")
                pending += d
                while b"\n" in pending:
                    ln, pending = pending.split(b"\n", 1)
                    log(ln.decode("utf-8", "replace").rstrip("\r"))
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
    except Exception as e:  # noqa: BLE001
        log("## dropped: %r" % (e,))
        time.sleep(3)
