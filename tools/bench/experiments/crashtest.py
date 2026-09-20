import sys, time
sys.path.insert(0, "/Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main/tools/bench")
import tdeck_harness as h
s = h.TDeckSession("/dev/cu.usbmodem1101", 115200, 40.0)
s.open()
idx = s.send("--pos"); m = s.wait_for(r"[Ll]at", 3.0, since=idx)
s.send("--tab 3"); time.sleep(2)
# nodes around Freising (48.4076 N / 11.7386 E): 1 km, 3 km, 12 km, 60 km away
for call, lat, lon in [("DK5EN-01", 48.4166, 11.7386), ("DK5EN-02", 48.4076, 11.7790), ("DK5EN-03", 48.3000, 11.7386), ("DK5EN-04", 48.4076, 12.5500)]:
    idx = s.send(f"--injectpos {call} {lat} {lon}")
    m = s.wait_for(r"\[INJECTPOS\]", 3.0, since=idx); print("   ", m.string.strip()[-60:] if m else "no reply", flush=True)
    time.sleep(0.5)
crashed = None
for rep in range(3):
    for d in ["in"]*6 + ["out"]*6:
        idx = s.send(f"--mapzoom {d}")
        m = s.wait_for(r"\[MAPZOOM\]|Guru Meditation|rst:0x|Backtrace", 8.0, since=idx)
        if m is None or "MAPZOOM" not in m.string:
            crashed = (rep, d, m.string.strip()[:100] if m else "timeout"); break
        time.sleep(0.3)
    if crashed: break
idx = s.send("--uistat"); m = s.wait_for(r"\[UISTAT\]", 3.0, since=idx)
print("crashed:", crashed, flush=True)
print("uistat:", (m.string.strip()[:160] if m else "none"), flush=True)
s.close(); print("=== DONE", flush=True)
