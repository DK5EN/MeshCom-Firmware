import sys, time
sys.path.insert(0, "/Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main/tools/bench")
import tdeck_harness as h
s = h.TDeckSession("/dev/cu.usbmodem1101", 115200, 40.0)
s.open()
s.send("--tab 0"); time.sleep(1)
s.send("--redrawlog on"); time.sleep(0.3)
s.send("--mute on"); time.sleep(1)
for i in range(3): s.send(f"--injectmsg 9999 muted {i}"); time.sleep(4)
s.send("--mute off"); time.sleep(1)
s.send("--audiodbg 2"); time.sleep(1)
for i in range(3): s.send(f"--injectmsg 9999 sdonly {i}"); time.sleep(4)
s.send("--redrawlog off"); time.sleep(0.5); s.close(); print("=== DONE", flush=True)
