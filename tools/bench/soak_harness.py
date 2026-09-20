#!/usr/bin/env python3
"""N-20 cable-flap soak harness.

Three channels against the bench RAK4631 (DK5EN-90, 192.168.68.68):
  1. UDP listener on :1799  - receives the node's EXTUDP stream including the
     MC_TEST_HOOKS heartbeat ({"type":"hb","seq":N,"ms":M} every 500 ms).
     Logs seq gaps and stream outages.
  2. DM injector             - every 10 s sends {"type":"msg","dst":"DK5EN-91",...}
     to the node, driving LoRa TX + DATA upload (port-1990 socket) + ack traffic.
  3. Serial echo probe       - every 10 s sends --pos over USB CDC; serial bytes
     arriving prove the loop task is alive regardless of Ethernet state.
     A probe window with zero serial bytes => STALL event (the actual verdict).

All events go to soak_log.txt with wall-clock timestamps. Ctrl-C / SIGTERM to stop.
"""
import json
import signal
import socket
import sys
import threading
import time

import serial

NODE_IP = "192.168.68.68"
EXT_PORT = 1799
SERIAL_PORT = "/dev/cu.usbmodem2101"
LOG = "./soak_log.txt"

stop = threading.Event()
lock = threading.Lock()
logf = open(LOG, "a", buffering=1)


def log(tag, msg):
    line = f"{time.strftime('%H:%M:%S')} [{tag}] {msg}"
    with lock:
        logf.write(line + "\n")
    print(line, flush=True)


state = {
    "last_seq": None,
    "last_hb_wall": None,
    "hb_count": 0,
    "ext_count": 0,
    "seq_gaps": 0,
    "outages": [],       # (start_wall, end_wall, missed)
    "outage_start": None,
    "serial_stalls": [],
    "last_serial_wall": time.time(),
    "inject_n": 0,
}


def listener():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("", EXT_PORT))
    sock.settimeout(0.5)
    while not stop.is_set():
        try:
            data, addr = sock.recvfrom(4096)
        except socket.timeout:
            # watchdog: heartbeat silence > 2 s = stream outage begins
            if state["last_hb_wall"] and state["outage_start"] is None:
                if time.time() - state["last_hb_wall"] > 2.0:
                    state["outage_start"] = state["last_hb_wall"]
                    log("HB", f"stream OUTAGE begins (last seq {state['last_seq']})")
            continue
        except OSError:
            continue
        if addr[0] != NODE_IP:
            continue
        now = time.time()
        try:
            j = json.loads(data.decode("utf-8", "replace"))
        except ValueError:
            continue
        if j.get("type") == "hb":
            seq = int(j.get("seq", -1))
            if state["outage_start"] is not None:
                dur = now - state["outage_start"]
                missed = (seq - state["last_seq"] - 1) if state["last_seq"] is not None else -1
                state["outages"].append((state["outage_start"], now, missed))
                log("HB", f"stream RESUMED after {dur:.1f}s, seq {state['last_seq']} -> {seq} ({missed} missed)")
                state["outage_start"] = None
            elif state["last_seq"] is not None and seq != state["last_seq"] + 1:
                state["seq_gaps"] += 1
                log("HB", f"seq gap {state['last_seq']} -> {seq}")
            state["last_seq"] = seq
            state["last_hb_wall"] = now
            state["hb_count"] += 1
            if state["hb_count"] == 1 or state["hb_count"] % 240 == 0:  # first + every ~2 min
                log("HB", f"alive: seq={seq}, {state['hb_count']} heartbeats, {state['ext_count']} other datagrams")
        else:
            state["ext_count"] += 1
            log("EXT", f"{j.get('src_type','?')}/{j.get('type','?')} src={j.get('src','')} msg={str(j.get('msg',''))[:40]}")


def injector():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    while not stop.is_set():
        state["inject_n"] += 1
        payload = json.dumps({"type": "msg", "dst": "DK5EN-91",
                              "msg": f"soak {state['inject_n']}"})
        try:
            sock.sendto(payload.encode(), (NODE_IP, EXT_PORT))
            log("INJ", f"sent DM #{state['inject_n']}")
        except OSError as e:
            log("INJ", f"send failed: {e}")
        stop.wait(10)


def serial_probe():
    s = None
    while not stop.is_set():
        if s is None:
            try:
                s = serial.Serial(SERIAL_PORT, 115200, timeout=0.2)
                log("SER", "port open")
            except Exception as e:
                log("SER", f"port open failed: {e}")
                stop.wait(2)
                continue
        got = b""
        try:
            s.write(b"--pos\r")
            s.flush()
            end = time.time() + 5
            while time.time() < end:
                chunk = s.read(2048)
                if chunk:
                    got += chunk
        except Exception as e:
            log("SER", f"port error: {e} (device re-enumerating?)")
            try:
                s.close()
            except Exception:
                pass
            s = None
            stop.wait(2)
            continue
        now = time.time()
        if got:
            state["last_serial_wall"] = now
        else:
            stall = now - state["last_serial_wall"]
            state["serial_stalls"].append(now)
            log("SER", f"*** NO ECHO (silent for {stall:.0f}s) -- possible loop stall ***")
        stop.wait(5)
    if s:
        s.close()


def summary(*_):
    stop.set()
    time.sleep(1)
    log("SUM", f"heartbeats={state['hb_count']} datagrams={state['ext_count']} "
               f"injected={state['inject_n']} seq_gaps={state['seq_gaps']} "
               f"outages={len(state['outages'])} serial_stall_events={len(state['serial_stalls'])}")
    for i, (a, b, m) in enumerate(state["outages"]):
        log("SUM", f"outage {i+1}: {time.strftime('%H:%M:%S', time.localtime(a))}"
                   f" -> {time.strftime('%H:%M:%S', time.localtime(b))}"
                   f" ({b-a:.1f}s, {m} hb missed)")
    sys.exit(0)


signal.signal(signal.SIGTERM, summary)
signal.signal(signal.SIGINT, summary)

log("RUN", "soak harness started -- pull the cable whenever you like")
threads = [threading.Thread(target=listener, daemon=True),
           threading.Thread(target=injector, daemon=True),
           threading.Thread(target=serial_probe, daemon=True)]
for t in threads:
    t.start()
while not stop.is_set():
    time.sleep(1)
