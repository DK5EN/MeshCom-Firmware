import sys, time
sys.path.insert(0, "/Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main/tools/bench")
import tdeck_harness as h
s = h.TDeckSession("/dev/cu.usbmodem1101", 115200, 40.0)
s.open()
s.send("--tab 0"); time.sleep(1)
s.send("--mute on"); time.sleep(1)
s.send("--invalidate"); time.sleep(1)
print("=== BLINK 20 (10 s): watch for skipped toggles", flush=True)
idx = s.send("--blink 20")
s.wait_for(r"\[BLINK\];done", 30.0, since=idx)
time.sleep(1); s.close(); print("=== DONE", flush=True)
