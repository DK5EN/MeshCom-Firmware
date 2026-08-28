import sys, time
sys.path.insert(0, "/Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main/tools/bench")
import tdeck_harness as h
mute = sys.argv[1] if len(sys.argv) > 1 else "off"
s = h.TDeckSession("/dev/cu.usbmodem1101", 115200, 40.0)
s.open()
s.send("--redrawlog on"); time.sleep(0.3)
s.send("--tab 0"); time.sleep(1)
s.send(f"--mute {mute}"); time.sleep(1.5)
for i in range(3):
    print(f"=== rep {i}: message", flush=True)
    s.send(f"--injectmsg 9999 CRC test {i}"); time.sleep(4)
    print(f"=== rep {i}: invalidate", flush=True)
    s.send("--invalidate"); time.sleep(4)
s.send("--redrawlog off"); time.sleep(0.5); s.close(); print("=== DONE", flush=True)
