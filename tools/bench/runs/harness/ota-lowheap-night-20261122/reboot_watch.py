#!/usr/bin/env python3
"""Night helper: detect ESP32 reboots (uptime in [INSTR-HEAP] goes backwards), record them, re-send --heaplog 60.
Console is single-client: uses the meshlogger PAUSE file, skips while the 06:00 trigger holds it."""
import glob, os, re, sys, time
from datetime import datetime
sys.path.insert(0, "/Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main/tools")
import mc_console
OUT = os.path.dirname(os.path.abspath(__file__))
NODES = {"DK5EN-1": "192.168.68.78", "DK5EN-92": "192.168.68.61", "DK5EN-14": "192.168.68.77", "DK5EN-98": "192.168.68.65"}
END = time.mktime(time.strptime("2026-11-23 07:30", "%Y-%m-%d %H:%M"))
RX = re.compile(r"\[INSTR-HEAP\][; ]up_s[; ](\d+)")
last = {}
def say(m):
    with open(os.path.join(OUT, "reboot-events.log"), "a") as f:
        f.write(f"{datetime.now():%Y-%m-%d %H:%M:%S} {m}\n")
def up_now(call):
    fs = sorted(glob.glob(os.path.join(OUT, call.lower(), "20*.log")))
    if not fs: return None
    with open(fs[-1], "rb") as f:
        f.seek(0, 2); f.seek(max(0, f.tell() - 20000))
        v = RX.findall(f.read().decode(errors="replace"))
    return int(v[-1]) if v else None
def resend(call, host):
    pause = os.path.join(OUT, call.lower(), "PAUSE")
    if os.path.exists(pause): return False       # trigger (or operator) holds the console
    open(pause, "w").close(); time.sleep(15)
    try:
        c = mc_console.Console(host, call=call); c.connect(); c.send("--heaplog 60"); time.sleep(2); c.close()
        return True
    finally:
        try: os.unlink(pause)
        except FileNotFoundError: pass
say("watcher start")
while time.time() < END:
    for call, host in NODES.items():
        u = up_now(call)
        if u is None: continue
        p = last.get(call)
        if p is not None and u < p - 5:
            say(f"{call} REBOOT detected (up_s {p} -> {u})")
            try: say(f"{call} heaplog 60 re-sent: {resend(call, host)}")
            except Exception as e: say(f"{call} resend failed: {e}")
        last[call] = u
    time.sleep(30)
say("watcher end")
