import sys, time
sys.path.insert(0, "/Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main/tools/bench")
import tdeck_harness as h
s = h.TDeckSession("/dev/cu.usbmodem1101", 115200, 40.0)
s.open()
s.send("--redrawlog on"); time.sleep(0.3)
s.send("--tab 0"); time.sleep(1)
s.send("--mute on"); time.sleep(1.5)
def go(label, cmds, wait):
    print(f"=== {label}", flush=True)
    for c in cmds: s.send(c); time.sleep(0.25)
    time.sleep(wait)
go("A: SD access + muted message -- watch", ["--sdtest", "--injectmsg 9999 A after SD access"], 7)
go("B: tone + muted message -- watch", ["--playtone msg", "--injectmsg 9999 B after tone"], 7)
go("C: muted message alone -- watch", ["--injectmsg 9999 C control"], 7)
s.send("--redrawlog off"); time.sleep(0.5); s.close(); print("=== DONE", flush=True)
