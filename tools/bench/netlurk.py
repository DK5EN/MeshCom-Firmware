#!/usr/bin/env python3
"""Passive, reconnecting listener on a node's net console (TCP 2323).

Unlike meshlogger.py it sends nothing to the node (no debug flags), and
unlike netconsole_log.py it survives a node reboot: when the socket drops it
logs a marker and reconnects every 3 s until --seconds elapse. Use it as the
second witness during a serial-driven toggle run -- a TCP drop plus a fresh
"CLIENT SETUP" banner pins a reboot even if the serial capture is lost.

Usage:
  netlurk.py HOST --out LOG [--seconds N] [--password PW]

Log line format:  <ISO wall clock>  <line>
Markers:          ### connected / ### lost (<reason>) / ### giving up
"""
import argparse
import datetime as dt
import hashlib
import hmac
import socket
import sys
import time


def stamp() -> str:
    return dt.datetime.now().isoformat(timespec="milliseconds")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("host")
    ap.add_argument("--out", required=True)
    ap.add_argument("--seconds", type=float, default=3600)
    ap.add_argument("--password", default="")
    ap.add_argument("--port", type=int, default=2323)
    ap.add_argument("--idle", type=float, default=45,
                    help="seconds without a byte before the link counts as dead (a rebooted node sends no FIN)")
    a = ap.parse_args()
    end = time.time() + a.seconds
    out = open(a.out, "a", buffering=1)

    def log(line: str) -> None:
        out.write(f"{stamp()}  {line}\n")

    while time.time() < end:
        try:
            s = socket.create_connection((a.host, a.port), timeout=5)
        except OSError as e:
            log(f"### connect failed ({e})")
            time.sleep(3)
            continue
        s.settimeout(1.0)
        log("### connected")
        pending = b""
        last = time.time()
        try:
            while time.time() < end:
                try:
                    d = s.recv(4096)
                except socket.timeout:
                    if time.time() - last > a.idle:
                        log(f"### lost (idle {a.idle:.0f} s, node rebooted?)")
                        break
                    continue
                last = time.time()
                if not d:
                    log("### lost (eof)")
                    break
                pending += d
                while b"\n" in pending:
                    line, pending = pending.split(b"\n", 1)
                    text = line.decode("utf-8", "replace").rstrip("\r")
                    if text.startswith("NONCE: ") and a.password:
                        nonce = bytes.fromhex(text.split()[1])
                        resp = hmac.new(a.password.encode(), nonce, hashlib.sha256).hexdigest()
                        s.send((resp + "\r\n").encode())
                    log(text)
        except OSError as e:
            log(f"### lost ({e})")
        finally:
            try:
                s.close()
            except OSError:
                pass
        time.sleep(3)
    log("### giving up (time elapsed)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
