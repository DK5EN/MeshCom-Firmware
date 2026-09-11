"""Node-side log over the net console (2323), for boards whose USB CDC drops.

The T-Deck's native-USB CDC re-enumerated 63 times in 80 s while logging an
instrument image during a UDP replay, losing more than half the [GW];rx lines.
The net console carries the same MSerial stream over TCP and does not.
"""
import socket, sys, time, threading
host, out_path, seconds = sys.argv[1], sys.argv[2], float(sys.argv[3])
cmds = sys.argv[4:]
s = socket.create_connection((host, 2323), timeout=10)
s.settimeout(0.5)
buf = []
stop = threading.Event()

def rx():
    pending = b""
    while not stop.is_set():
        try:
            d = s.recv(4096)
            if not d:
                break
            pending += d
            while b"\n" in pending:
                line, pending = pending.split(b"\n", 1)
                buf.append(line.decode("utf-8", "replace").rstrip("\r"))
        except socket.timeout:
            continue
        except Exception:
            break

t = threading.Thread(target=rx, daemon=True); t.start()
time.sleep(1.5)
for c in cmds:
    for ch in c + "\n":
        s.send(ch.encode()); time.sleep(0.02)
    time.sleep(1.0)
    print(">>", c, file=sys.stderr)
time.sleep(seconds)
stop.set(); time.sleep(0.8); s.close()
open(out_path, "w").write("\n".join(buf) + "\n")
print(f"{len(buf)} lines -> {out_path}", file=sys.stderr)
