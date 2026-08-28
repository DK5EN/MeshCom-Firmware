import sys, time
sys.path.insert(0, "/Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main/tools/bench")
import tdeck_harness as h
label = sys.argv[1]; extra = sys.argv[2:]  # extra commands after the message
s = h.TDeckSession("/dev/cu.usbmodem1101", 115200, 40.0)
s.open()
s.send("--redrawlog on"); time.sleep(0.3)
s.send("--tab 0"); time.sleep(1)
s.send("--mute off"); time.sleep(1.5)
s.send(f"--injectmsg 9999 {label}"); time.sleep(4)
for c in extra:
    s.send(c); time.sleep(3)
time.sleep(6)
s.send("--redrawlog off"); time.sleep(0.5); s.close(); print("=== DONE", flush=True)
