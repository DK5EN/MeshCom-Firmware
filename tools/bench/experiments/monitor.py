import sys, time, re
sys.path.insert(0, "/Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main/tools/bench")
import tdeck_harness as h
s = h.TDeckSession("/dev/cu.usbmodem1101", 115200, 40.0)
s.open()
s.send("--heap start"); time.sleep(0.5)
print("=== MONITOR RUNNING - reproduce the crash now (map tab, zoom in fully, zoom out)", flush=True)
t0 = time.monotonic(); last = s.length()
while time.monotonic() - t0 < 900:
    m = s.wait_for(r"Guru Meditation|Backtrace:|abort\(\)|rst:0x|panic|assert", 5.0, since=last)
    if m:
        print("=== EVENT:", m.string[:120], flush=True)
        last = s.length()
        if "rst:0x" in m.string:
            time.sleep(3)
            break
s.close(); print("=== DONE", flush=True)
