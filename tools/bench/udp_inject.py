#!/usr/bin/env python3
"""Send one corpus datagram (hex file) to a node's UDP port.

The files under test/golden/corpus/udp1990/ hold one datagram each as hex
with '#' comment lines. This sends exactly one of them and prints what went
out, for bench proofs such as H6-01 (a zero-padded datagram must not take the
WiFi down).

Usage:
  udp_inject.py HOST PORT FILE [--repeat N] [--gap SECS]
"""
import argparse
import socket
import sys
import time


def load_hex(path: str) -> bytes:
    text = "".join(
        line.split("#", 1)[0].strip() for line in open(path, encoding="utf-8")
    )
    return bytes.fromhex(text)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("host")
    ap.add_argument("port", type=int)
    ap.add_argument("file")
    ap.add_argument("--repeat", type=int, default=1)
    ap.add_argument("--gap", type=float, default=1.0)
    a = ap.parse_args()
    data = load_hex(a.file)
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    for i in range(a.repeat):
        s.sendto(data, (a.host, a.port))
        print(f"sent {len(data)} B to {a.host}:{a.port} head {data[:4].hex()} ({i + 1}/{a.repeat})", flush=True)
        if i + 1 < a.repeat:
            time.sleep(a.gap)
    return 0


if __name__ == "__main__":
    sys.exit(main())
