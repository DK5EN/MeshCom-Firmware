import sys, time
sys.path.insert(0, "/Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main/tools/bench")
import tdeck_harness as h
s = h.TDeckSession("/dev/cu.usbmodem1101", 115200, 40.0)
s.open()
s.send("--redrawlog on"); time.sleep(0.3)
s.send("--tab 0"); time.sleep(1)
def go(label, cmd, wait):
    print(f"=== {label}", flush=True)
    s.send(cmd); time.sleep(wait)
go("MUTE ON", "--mute on", 1.5)
go("MESSAGE 1 (muted) -- watch", "--injectmsg 9999 MUTED one", 6)
go("MESSAGE 2 (muted) -- watch", "--injectmsg 9999 MUTED two", 6)
go("MUTE OFF", "--mute off", 1.5)
go("MESSAGE 3 (tone) -- watch", "--injectmsg 9999 TONE three", 6)
s.send("--redrawlog off"); time.sleep(0.5); s.close(); print("=== DONE", flush=True)
