import sys, time, re
sys.path.insert(0, "/Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main/tools/bench")
import tdeck_harness as h
s = h.TDeckSession("/dev/cu.usbmodem1101", 115200, 40.0)
s.open()
s.send("--tab 3"); time.sleep(2)
seq = ["in"]*6 + ["out"]*6
for d in seq:
    idx = s.send(f"--mapzoom {d}")
    m = s.wait_for(r"\[MAP\];zoom;", 6.0, since=idx)
    print("   ", m.string.strip()[-110:] if m else "no [MAP] line", flush=True)
    time.sleep(1.5)
s.close(); print("=== DONE", flush=True)
