#!/usr/bin/env python3
"""BF-01 bench: wrong-tag RM frames never lock with --rmstrictsecurity off, lock at the third with on.

Sender and target are both own bench nodes on USB; the frames are direct messages
from the sender to the target (never a group, never a broadcast). No password is
needed: the tags are deliberately wrong (random 16 hex), so the target rejects them
with [RM];reject;tag, and with strict on the third strike arms the per-sender lock.

Usage: rm_strict_bench.py --target PORT --sender PORT --dst DK5EN-1 [--frames 4] [--gap 4]
"""
import argparse, random, sys, time
import serial

BOOT = "[BOOT];ready"


def open_port(path):
    s = serial.Serial()
    s.port = path
    s.baudrate = 115200
    s.timeout = 0.2
    s.dtr = False  # CP2102 bridge: DTR/RTS drive EN/BOOT; keep them low
    s.rts = False
    s.open()
    return s


def drain(s, seconds, sink, want=None):
    end = time.monotonic() + seconds
    buf = b""
    while time.monotonic() < end:
        chunk = s.read(512)
        if chunk:
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                t = line.decode("utf-8", "replace").rstrip("\r")
                sink.append(t)
                if want and want in t:
                    return True
    return False


def send(s, text):
    s.write((text + "\n").encode())
    s.flush()


def wait_boot(name, s, sink):
    if drain(s, 60, sink, BOOT):
        time.sleep(2.0)
        print(f"{name}: boot marker seen", file=sys.stderr)
    else:
        print(f"{name}: no boot marker in 60 s, continuing", file=sys.stderr)


def run_round(label, target, sender, dst, frames, gap, ctr0, log):
    seen = []
    for i in range(frames):
        ctr = ctr0 + i
        tag = "%016x" % random.getrandbits(64)
        send(sender, "::{%s}RM1 %d status %s" % (dst, ctr, tag))
        lines = []
        drain(target, gap, lines)
        log.extend(lines)
        rej = [l for l in lines if "[RM];reject;" in l]
        rx = [l for l in lines if "RM1 " in l and ("RX-LoRa" in l or "[LOG]" in l)]
        print(f"{label} frame {i+1}: {len(rx)} RM1 frame(s) seen on air", file=sys.stderr)
        seen.extend(rej)
        print(f"{label} frame {i+1}: " + (", ".join(r.strip() for r in rej) if rej else "no reject marker"), file=sys.stderr)
    lines = []
    drain(target, 6, lines)
    log.extend(lines)
    seen.extend(l for l in lines if "[RM];reject;" in l)
    return seen


def set_strict(target, on, log):
    lines = []
    send(target, "--rmstrictsecurity %s" % ("on" if on else "off"))
    ok = drain(target, 5, lines, "[RM];strict=%s" % ("on" if on else "off"))
    log.extend(lines)
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--target", required=True)
    ap.add_argument("--sender", required=True)
    ap.add_argument("--dst", required=True, help="target call with SSID, e.g. DK5EN-1")
    ap.add_argument("--frames", type=int, default=4)
    ap.add_argument("--gap", type=float, default=4.0)
    ap.add_argument("--log", default=None)
    a = ap.parse_args()
    log = []
    t = open_port(a.target)
    s = open_port(a.sender)
    wait_boot("target", t, log)
    wait_boot("sender", s, log)
    drain(t, 2, log)
    drain(s, 2, log)
    send(t, "--loradebug on"); drain(t, 2, log)

    verdict = []
    if not set_strict(t, False, log):
        verdict.append("FAIL: target did not confirm --rmstrictsecurity off")
    off = run_round("OFF", t, s, a.dst, a.frames, a.gap, 9001, log)
    tags = sum("reject;tag" in l for l in off)
    locks = sum("reject;lockout" in l for l in off)
    disabled = sum("reject;disabled" in l for l in off)
    if disabled:
        verdict.append("INCONCLUSIVE: target rejects with 'disabled' (remote management off or no password)")
    elif tags == a.frames and locks == 0:
        verdict.append(f"PASS OFF: {tags} tag rejects, no lock")
    else:
        verdict.append(f"FAIL OFF: {tags} tag rejects, {locks} lockout rejects (expected {a.frames}/0)")

    if not set_strict(t, True, log):
        verdict.append("FAIL: target did not confirm --rmstrictsecurity on")
    on = run_round("ON", t, s, a.dst, a.frames, a.gap, 9101, log)
    tags = sum("reject;tag" in l for l in on)
    locks = sum("reject;lockout" in l for l in on)
    if tags == 3 and locks == a.frames - 3:
        verdict.append(f"PASS ON: 3 tag rejects, then {locks} lockout reject(s)")
    else:
        verdict.append(f"FAIL ON: {tags} tag rejects, {locks} lockout rejects (expected 3/{a.frames-3})")

    # restore the default and let the OFF sweep release the lock
    set_strict(t, False, log)
    send(t, "--loradebug off"); drain(t, 2, log)
    t.close(); s.close()
    if a.log:
        with open(a.log, "w") as f:
            f.write("\n".join(log) + "\n")
    print("\n".join(verdict))
    return 0 if all(v.startswith("PASS") for v in verdict) else 1


if __name__ == "__main__":
    sys.exit(main())
